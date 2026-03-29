//
// Created by hyp on 2026/3/26.
//

#include "chunker.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {
    // 语义块类型决定二次切分策略：
    // 1. 标题通常很短，优先整体保留
    // 2. 普通段落更适合按句子切
    // 3. 列表和代码块更适合按行切
    // 这样可以避免“标题被拖到下一块”或“代码从中间断开”这类典型 RAG 噪声。
    enum class BlockType {
        Heading,
        Paragraph,
        List,
        Code
    };

    // ChunkUnit 是“可装箱”的最小语义单元。
    // text 保存真实内容，char_count 则缓存 UTF-8 字符数，避免后续装箱和 overlap 回填时
    // 对同一段文本反复做 UTF-8 扫描。
    struct ChunkUnit {
        std::string text;
        std::size_t char_count{0};
    };

    // 安全阈值：语义切分只能作为上传期的轻量预处理，超过这个数量基本已经不是正常文档。
    constexpr std::size_t kMaxChunkCount = 4096;

    // 根据 UTF-8 首字节推断当前字符的字节长度；非法起始字节按 1 字节兜底处理。
    std::size_t utf8_char_length(unsigned char lead) {
        if ((lead & 0x80) == 0x00) {
            return 1;
        }
        if ((lead & 0xE0) == 0xC0) {
            return 2;
        }
        if ((lead & 0xF0) == 0xE0) {
            return 3;
        }
        if ((lead & 0xF8) == 0xF0) {
            return 4;
        }
        return 1;
    }

    // 预计算每个 UTF-8 字符在原始字节串中的起始位置，便于按“字符数”安全截断。
    std::vector<std::size_t> build_utf8_char_offsets(const std::string &text) {
        std::vector<std::size_t> offsets;
        offsets.reserve(text.size() + 1);
        std::size_t i = 0;
        while (i < text.size()) {
            offsets.push_back(i);
            const std::size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
            i += std::min(char_len, text.size() - i);
        }
        offsets.push_back(text.size());
        return offsets;
    }

    // 当前项目统一按 UTF-8 字符数估算 chunk 大小，至少不会把中文按字节数放大。
    std::size_t utf8_char_count(const std::string &text) {
        const auto offsets = build_utf8_char_offsets(text);
        return offsets.empty() ? 0 : offsets.size() - 1;
    }

    // 只清理首尾空白，保留正文内部换行，避免 Markdown 结构被抹平。
    std::string trim(const std::string &text) {
        std::size_t begin = 0;
        while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
            ++begin;
        }

        std::size_t end = text.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            --end;
        }
        return text.substr(begin, end - begin);
    }

    bool starts_with(const std::string &text, const std::string &prefix) {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    // 句子边界扫描会在热点循环里反复调用这个函数。
    // 直接按偏移 compare 可以避免先 substr 再 compare 产生额外临时字符串。
    bool starts_with_at(const std::string &text, std::size_t offset, const std::string &prefix) {
        return offset <= text.size()
               && text.size() - offset >= prefix.size()
               && text.compare(offset, prefix.size(), prefix) == 0;
    }

    // 空白行是段落边界。
    bool is_blank_line(const std::string &line) {
        return trim(line).empty();
    }

    // 只识别标准 Markdown 标题，避免把普通井号前缀误判成标题块。
    bool is_heading_line(const std::string &line) {
        const std::string content = trim(line);
        if (content.empty() || content[0] != '#') {
            return false;
        }

        std::size_t level = 0;
        while (level < content.size() && content[level] == '#') {
            ++level;
        }
        return level > 0 && level < content.size() && content[level] == ' ';
    }

    // fenced code block 单独成块，避免代码和普通文本混切。
    bool is_fence_line(const std::string &line) {
        const std::string content = trim(line);
        return starts_with(content, "```") || starts_with(content, "~~~");
    }

    // 列表项优先按条目聚合，语义上通常比按字符滑窗更稳定。
    bool is_list_line(const std::string &line) {
        const std::string content = trim(line);
        if (content.size() >= 2) {
            const char lead = content[0];
            if ((lead == '-' || lead == '*' || lead == '+') && content[1] == ' ') {
                return true;
            }
        }

        std::size_t index = 0;
        while (index < content.size()
               && std::isdigit(static_cast<unsigned char>(content[index])) != 0) {
            ++index;
        }
        return index > 0
               && index + 1 < content.size()
               && content[index] == '.'
               && content[index + 1] == ' ';
    }

    // 保留换行，后续才能继续识别代码块和列表边界。
    std::vector<std::string> split_lines_keep_newline(const std::string &text) {
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start < text.size()) {
            const std::size_t newline_pos = text.find('\n', start);
            if (newline_pos == std::string::npos) {
                lines.push_back(text.substr(start));
                break;
            }
            lines.push_back(text.substr(start, newline_pos - start + 1));
            start = newline_pos + 1;
        }
        if (lines.empty() && !text.empty()) {
            lines.push_back(text);
        }
        return lines;
    }

    // 对超长文本做 UTF-8 安全硬切，作为最后兜底，不再尝试更复杂语义判断。
    // 这层兜底的目标不是“最优语义”，而是保证任何异常输入都能有确定上界，
    // 不会因为某个超长段落或一整块未换行文本把进程内存拖爆。
    std::vector<ChunkUnit> hard_split_text(const std::string &text, std::size_t chunk_size) {
        std::vector<ChunkUnit> pieces;
        const auto offsets = build_utf8_char_offsets(text);
        const std::size_t total_chars = offsets.size() - 1;
        std::size_t start_char = 0;
        while (start_char < total_chars) {
            const std::size_t end_char = std::min(start_char + chunk_size, total_chars);
            const std::size_t start_byte = offsets[start_char];
            const std::size_t end_byte = offsets[end_char];
            const std::string piece = trim(text.substr(start_byte, end_byte - start_byte));
            if (!piece.empty()) {
                pieces.push_back({piece, end_char - start_char});
                if (pieces.size() > kMaxChunkCount) {
                    throw std::runtime_error("text produced too many fallback chunks");
                }
            }
            start_char = end_char;
        }
        return pieces;
    }

    bool is_sentence_break_char(char ch) {
        switch (ch) {
            case '\n':
            case '.':
            case '!':
            case '?':
            case ';':
            case ':':
                return true;
            default:
                return false;
        }
    }

    // 句子边界同时兼容中英文标点；这里故意保持简单，目标是稳定而不是语言学完美。
    bool is_sentence_break_at(const std::string &text, std::size_t byte_pos) {
        if (byte_pos >= text.size()) {
            return false;
        }

        const unsigned char ch = static_cast<unsigned char>(text[byte_pos]);
        if (ch < 0x80) {
            return is_sentence_break_char(static_cast<char>(ch));
        }

        return starts_with_at(text, byte_pos, "。")
               || starts_with_at(text, byte_pos, "！")
               || starts_with_at(text, byte_pos, "？")
               || starts_with_at(text, byte_pos, "；")
               || starts_with_at(text, byte_pos, "：");
    }

    // 段落文本优先按句子切分，尽量让单个 chunk 停在自然边界上。
    // 如果一句话本身就比 chunk_size 还长，则继续回退到 UTF-8 安全硬切。
    // 这样可以覆盖两类极端输入：
    // 1. 长文里正常的多句段落
    // 2. 几乎没有标点或换行的异常长句
    std::vector<ChunkUnit> split_paragraph_into_units(const std::string &text, std::size_t chunk_size) {
        std::vector<ChunkUnit> units;
        const auto offsets = build_utf8_char_offsets(text);
        const std::size_t total_chars = offsets.size() - 1;
        std::size_t sentence_start = 0;

        for (std::size_t i = 0; i < total_chars; ++i) {
            if (!is_sentence_break_at(text, offsets[i])) {
                continue;
            }

            const std::size_t start_byte = offsets[sentence_start];
            const std::size_t end_byte = offsets[i + 1];
            const std::string sentence = trim(text.substr(start_byte, end_byte - start_byte));
            if (!sentence.empty()) {
                const std::size_t char_count = i + 1 - sentence_start;
                if (char_count <= chunk_size) {
                    units.push_back({sentence, char_count});
                } else {
                    const auto fallback_pieces = hard_split_text(sentence, chunk_size);
                    units.insert(units.end(), fallback_pieces.begin(), fallback_pieces.end());
                }
                if (units.size() > kMaxChunkCount) {
                    throw std::runtime_error("paragraph produced too many sentence units");
                }
            }
            sentence_start = i + 1;
        }

        if (sentence_start < total_chars) {
            const std::size_t start_byte = offsets[sentence_start];
            const std::string tail = trim(text.substr(start_byte));
            if (!tail.empty()) {
                const std::size_t tail_char_count = total_chars - sentence_start;
                if (tail_char_count <= chunk_size) {
                    units.push_back({tail, tail_char_count});
                } else {
                    const auto fallback_pieces = hard_split_text(tail, chunk_size);
                    units.insert(units.end(), fallback_pieces.begin(), fallback_pieces.end());
                }
            }
        }

        return units;
    }

    // 列表和代码块优先按行切分，比按句子更符合原始结构。
    // 例如代码块里一行通常就是一个完整语义片段，而按句号切几乎没有意义。
    std::vector<ChunkUnit> split_line_based_block(const std::string &text, std::size_t chunk_size) {
        std::vector<ChunkUnit> units;
        for (const auto &line: split_lines_keep_newline(text)) {
            const std::string content = trim(line);
            if (content.empty()) {
                continue;
            }

            const std::size_t char_count = utf8_char_count(content);
            if (char_count <= chunk_size) {
                units.push_back({content, char_count});
            } else {
                const auto fallback_pieces = hard_split_text(content, chunk_size);
                units.insert(units.end(), fallback_pieces.begin(), fallback_pieces.end());
            }
            if (units.size() > kMaxChunkCount) {
                throw std::runtime_error("line-based block produced too many units");
            }
        }
        return units;
    }

    // 把不同结构块转成“可装箱”的语义单元。
    // 这一层只负责“块内切分策略”，不关心 overlap，也不关心最终 chunk 边界。
    std::vector<ChunkUnit> build_units_for_block(const std::string &text,
                                                 BlockType type,
                                                 std::size_t chunk_size) {
        const std::string content = trim(text);
        if (content.empty()) {
            return {};
        }

        const std::size_t char_count = utf8_char_count(content);
        if (char_count <= chunk_size) {
            return {{content, char_count}};
        }

        switch (type) {
            case BlockType::Heading:
                return hard_split_text(content, chunk_size);
            case BlockType::Paragraph:
                return split_paragraph_into_units(content, chunk_size);
            case BlockType::List:
            case BlockType::Code:
                return split_line_based_block(content, chunk_size);
        }

        return hard_split_text(content, chunk_size);
    }

    // 最终写入向量库和数据库的文本仍然保留空行分隔，尽量维持原文阅读形态。
    std::string join_units(const std::vector<ChunkUnit> &units,
                           std::size_t start_index,
                           std::size_t end_index) {
        std::string joined;
        for (std::size_t i = start_index; i < end_index; ++i) {
            if (!joined.empty()) {
                joined += "\n\n";
            }
            joined += units[i].text;
        }
        return joined;
    }

    // ChunkAssembler 负责把“语义单元流”组装成最终 chunk。
    // 这里单独抽成一个小状态机，是为了把“结构识别”和“chunk 装箱”两件事拆开：
    // 前者关心怎么识别标题/列表/代码块，后者只关心大小预算与 overlap。
    class ChunkAssembler {
    public:
        ChunkAssembler(std::size_t chunk_size, std::size_t overlap)
            : chunk_size_(chunk_size), overlap_(overlap) {
        }

        // 追加一个结构块。块内部的二次切分在这里完成，调用方不需要关心细节。
        // 调用方只需要告诉装箱器“这是一段标题/列表/代码/普通段落”，
        // 后续应该怎样拆成单元，由装箱器内部统一决定。
        void append_block(const std::string &text, BlockType type) {
            const auto units = build_units_for_block(text, type, chunk_size_);
            for (const auto &unit: units) {
                append_unit(unit);
            }
        }

        // 收尾时把最后一个未落盘的 chunk 推入结果。
        // 这里显式关闭 overlap 回填，因为文档已经结束，没有必要再保留尾部上下文。
        std::vector<std::string> finish() {
            flush_current(false);
            return std::move(chunks_);
        }

    private:
        // append_unit 是真正的“装箱”入口。
        // 它会尽量把单元塞进当前 chunk；塞不下时先把当前 chunk 落盘，再尝试基于 overlap
        // 回填尾部上下文，然后继续装入新单元。
        void append_unit(const ChunkUnit &unit) {
            if (unit.text.empty()) {
                return;
            }

            // 当前 chunk 为空时，直接把第一个语义单元放进去即可。
            if (current_units_.empty()) {
                current_units_.push_back(unit);
                current_chars_ = unit.char_count;
                return;
            }

            // 这里把块间分隔符的成本也计入预算，避免实际拼出来的文本超出目标窗口。
            const std::size_t separator_chars = 2;
            if (current_chars_ + separator_chars + unit.char_count <= chunk_size_) {
                current_units_.push_back(unit);
                current_chars_ += separator_chars + unit.char_count;
                return;
            }

            // 当前 chunk 已经装不下新单元，先把它落盘；flush_current(true) 会尽量保留
            // 末尾 overlap 对应的完整语义单元，供下一个 chunk 复用上下文。
            flush_current(true);

            // overlap 回填后如果仍然装不下新单元，说明尾部上下文过长，必须丢弃 overlap，
            // 否则会在“保留尾部 -> 仍装不下 -> 再次落盘”之间循环。
            if (!current_units_.empty() && current_chars_ + 2 + unit.char_count > chunk_size_) {
                current_units_.clear();
                current_chars_ = 0;
            }

            current_units_.push_back(unit);
            current_chars_ = current_chars_ == 0 ? unit.char_count : current_chars_ + 2 + unit.char_count;
        }

        // flush_current 负责两件事：
        // 1. 把当前累计的语义单元拼成真正的 chunk 文本
        // 2. 根据 keep_overlap 决定是否把 chunk 尾部的一部分单元回填到下一个 chunk
        // overlap 只按完整语义单元回填，不会切半句或半条列表项。
        void flush_current(bool keep_overlap) {
            if (current_units_.empty()) {
                return;
            }

            chunks_.push_back(join_units(current_units_, 0, current_units_.size()));
            if (chunks_.size() > kMaxChunkCount) {
                throw std::runtime_error("document produced too many chunks");
            }

            if (!keep_overlap || overlap_ == 0) {
                current_units_.clear();
                current_chars_ = 0;
                return;
            }

            // 从尾部反向挑选能落进 overlap 预算的单元。
            // 反向扫描能保证“离 chunk 末尾最近的上下文”优先被保留。
            std::vector<ChunkUnit> overlap_units;
            std::size_t overlap_chars = 0;
            for (std::size_t i = current_units_.size(); i > 0; --i) {
                const auto &unit = current_units_[i - 1];
                const std::size_t separator_chars = overlap_units.empty() ? 0 : 2;
                if (overlap_chars + separator_chars + unit.char_count > overlap_) {
                    break;
                }
                overlap_chars += separator_chars + unit.char_count;
                overlap_units.insert(overlap_units.begin(), unit);
            }

            current_units_ = std::move(overlap_units);
            current_chars_ = 0;
            for (std::size_t i = 0; i < current_units_.size(); ++i) {
                current_chars_ += current_units_[i].char_count;
                if (i > 0) {
                    current_chars_ += 2;
                }
            }
        }

    private:
        std::size_t chunk_size_;
        std::size_t overlap_;
        std::size_t current_chars_{0};
        std::vector<ChunkUnit> current_units_;
        std::vector<std::string> chunks_;
    };
}

Chunker::Chunker(std::size_t chunk_size, std::size_t overlap)
    : chunk_size_(chunk_size), overlap_(overlap) {
    if (chunk_size_ == 0) {
        throw std::invalid_argument("chunk_size must be greater than 0");
    }
    if (overlap_ >= chunk_size_) {
        throw std::invalid_argument("overlap must be smaller than the chunk size");
    }
}

std::vector<std::string> Chunker::split(const std::string &text) const {
    std::vector<std::string> result;
    // 空文本直接返回。
    if (text.empty()) {
        return result;
    }

    ChunkAssembler assembler(chunk_size_, overlap_);
    std::string paragraph_buffer;
    std::string list_buffer;
    std::string code_buffer;
    bool in_code_block = false;

    // 普通段落在遇到空行、标题、列表、代码块起始时结束。
    const auto flush_paragraph = [&assembler, &paragraph_buffer]() {
        if (paragraph_buffer.empty()) {
            return;
        }
        assembler.append_block(paragraph_buffer, BlockType::Paragraph);
        paragraph_buffer.clear();
    };
    // 连续列表项聚合成一个结构块，再交给装箱器按行继续切。
    const auto flush_list = [&assembler, &list_buffer]() {
        if (list_buffer.empty()) {
            return;
        }
        assembler.append_block(list_buffer, BlockType::List);
        list_buffer.clear();
    };
    // fenced code block 在遇到结束 fence 前都视为同一块，避免代码正文被外层状态机误判。
    const auto flush_code = [&assembler, &code_buffer]() {
        if (code_buffer.empty()) {
            return;
        }
        assembler.append_block(code_buffer, BlockType::Code);
        code_buffer.clear();
    };

    // 第一层按 Markdown 结构流式切块，不把整篇文档先拆成一个超大中间数组。
    // 这个状态机只有一个核心目标：尽早识别结构边界，尽早把块交给装箱器处理，
    // 这样文档再大，也不会先在内存里保留一份“整篇文档的所有语义块列表”。
    for (const auto &line: split_lines_keep_newline(text)) {
        if (in_code_block) {
            code_buffer += line;
            if (is_fence_line(line)) {
                // 只有遇到结束 fence，整块代码才真正闭合并落盘。
                flush_code();
                in_code_block = false;
            }
            continue;
        }

        if (is_fence_line(line)) {
            // 代码块优先级最高。进入代码块前先结束掉普通段落和列表状态。
            flush_paragraph();
            flush_list();
            code_buffer = line;
            in_code_block = true;
            continue;
        }

        if (is_blank_line(line)) {
            flush_paragraph();
            flush_list();
            continue;
        }

        if (is_heading_line(line)) {
            // 标题天然是强边界：前面的正文必须先落盘，再单独处理标题本身。
            flush_paragraph();
            flush_list();
            assembler.append_block(line, BlockType::Heading);
            continue;
        }

        if (is_list_line(line)) {
            // 一旦进入列表态，后续连续列表项会被聚合，直到遇到空行或其他结构块。
            flush_paragraph();
            list_buffer += line;
            continue;
        }

        // 走到这里说明是普通正文；如果前面正在累计列表，需要先结束列表态。
        flush_list();
        paragraph_buffer += line;
    }

    // 文档结束时，把还没闭合的结构块全部收尾。
    flush_paragraph();
    flush_list();
    flush_code();
    return assembler.finish();
}
