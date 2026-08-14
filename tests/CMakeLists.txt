find_package(Qt6 6.5 REQUIRED COMPONENTS Core Test)

qt_add_executable(IrAutoXCoreTests
    TestCore.cpp
    ../src/core/ArchiveUtil.cpp
    ../src/core/ArchiveUtil.h
    ../src/core/MessageCodec.cpp
    ../src/core/MessageCodec.h
)

target_include_directories(IrAutoXCoreTests PRIVATE ../src)
target_compile_definitions(IrAutoXCoreTests PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_link_libraries(IrAutoXCoreTests PRIVATE Qt6::Core Qt6::Test)

add_test(NAME IrAutoXCoreTests COMMAND IrAutoXCoreTests)

