target_sources(${QTRAG_SERVER_TARGET}
        PRIVATE
        sqllite_store.h
        sqllite_store.cpp
)

add_subdirectory(repositories)