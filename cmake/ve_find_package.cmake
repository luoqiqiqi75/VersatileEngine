# =============================================================================
# ve_find_package.cmake — 统一三方库查找宏
# =============================================================================
#
# 用法:
#   ve_find_package(<PackageName>
#     [GIT_REPO  <url>]              # FetchContent 回退 git 仓库
#     [GIT_TAG   <tag>]              # FetchContent 回退 git 标签
#     [OPTIONS   <KEY VALUE>...]     # 子项目构建选项 (BOOL 类型)
#   )
#
# 查找顺序:
#   1. VE_DEP_<PackageName>  本地路径  → add_subdirectory
#   2. find_package(<PackageName>)    系统已安装
#   3. FetchContent                   从 GIT_REPO 下载
#
# 本地路径在 cmake/_local.cmake (gitignored) 中配置:
#   set(VE_DEP_nlohmann_json "D:/workspace/github/json" CACHE PATH "" FORCE)
#
# 示例:
#   ve_find_package(nlohmann_json
#     GIT_REPO  https://github.com/nlohmann/json.git
#     GIT_TAG   v3.11.3
#     OPTIONS   JSON_BuildTests OFF  JSON_Install OFF
#   )
# =============================================================================

include(FetchContent)

macro(ve_find_package _vfp_name)
    cmake_parse_arguments(_VFP "" "GIT_REPO;GIT_TAG" "OPTIONS" ${ARGN})

    # --- 设置子项目构建选项 (OPTIONS KEY VALUE ...) ---
    list(LENGTH _VFP_OPTIONS _vfp_opt_len)
    if(_vfp_opt_len GREATER 0)
        set(_vfp_i 0)
        while(_vfp_i LESS _vfp_opt_len)
            math(EXPR _vfp_j "${_vfp_i} + 1")
            list(GET _VFP_OPTIONS ${_vfp_i} _vfp_key)
            list(GET _VFP_OPTIONS ${_vfp_j} _vfp_val)
            set(${_vfp_key} ${_vfp_val} CACHE BOOL "" FORCE)
            math(EXPR _vfp_i "${_vfp_i} + 2")
        endwhile()
    endif()

    set(_vfp_found FALSE)

    # --- 1. 本地路径 (VE_DEP_<name> 由 _local.cmake 设置) ---
    if(DEFINED VE_DEP_${_vfp_name})
        if(EXISTS "${VE_DEP_${_vfp_name}}/CMakeLists.txt")
            add_subdirectory(
                "${VE_DEP_${_vfp_name}}"
                "${CMAKE_BINARY_DIR}/_deps/${_vfp_name}"
            )
            set(_vfp_found TRUE)
            message(STATUS "  ${_vfp_name}  [LOCAL]  ${VE_DEP_${_vfp_name}}")
        else()
            message(WARNING
                "ve_find_package: VE_DEP_${_vfp_name}=\"${VE_DEP_${_vfp_name}}\" "
                "does not contain CMakeLists.txt, falling back to find_package...")
        endif()
    endif()

    # --- 2. find_package (系统安装) ---
    if(NOT _vfp_found)
        find_package(${_vfp_name} QUIET)
        if(${_vfp_name}_FOUND)
            set(_vfp_found TRUE)
            message(STATUS "  ${_vfp_name}  [SYSTEM] ${${_vfp_name}_VERSION}")
        endif()
    endif()

    # --- 3. FetchContent (自动下载) ---
    if(NOT _vfp_found)
        if(_VFP_GIT_REPO)
            message(STATUS "  ${_vfp_name}  [FETCH]  ${_VFP_GIT_TAG}")
            FetchContent_Declare(${_vfp_name}
                GIT_REPOSITORY ${_VFP_GIT_REPO}
                GIT_TAG        ${_VFP_GIT_TAG}
                GIT_SHALLOW    TRUE
            )
            FetchContent_MakeAvailable(${_vfp_name})
            set(_vfp_found TRUE)
        endif()
    endif()

    # --- 未找到 → 报错 ---
    if(NOT _vfp_found)
        message(FATAL_ERROR
            "ve_find_package: cannot find '${_vfp_name}'.\n"
            "  Options:\n"
            "    1. Set VE_DEP_${_vfp_name} in cmake/_local.cmake\n"
            "    2. Install system-wide and ensure find_package can locate it\n"
            "    3. Provide GIT_REPO/GIT_TAG for automatic download"
        )
    endif()
endmacro()
