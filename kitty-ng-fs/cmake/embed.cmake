# Configure the existing SSH core without editing the client build files.
function(kg_add_plugin)
  include("${KITTYNGFS_DIR}/CMakeLists.txt")
endfunction()
cmake_language(DEFER CALL kg_add_plugin)
