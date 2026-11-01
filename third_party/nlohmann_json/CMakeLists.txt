# nlohmann/json 3.11.3, application-owned and independent of the SDK's private
# inja headers. The header and LICENSE.MIT came from the SDK's
# thirdparty/inja/third_party/include/nlohmann; SHA-256 of include/nlohmann/json.hpp
# with LF line endings: 9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6.
# Replace the header and the license together.
add_library(broken_bond_json INTERFACE)
add_library(broken_bond::json ALIAS broken_bond_json)
target_include_directories(broken_bond_json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
