project(
  'delivery_fleet_sim',
  ['c', 'cpp'],
  version : '1.0.0',
  meson_version : '>= 0.60.0',
  default_options : [
    'cpp_std=c++20',
    'c_std=c17',
    'warning_level=3',
    'werror=false',

    # ---- Shipping build: pure speed, no debug anything ----
    'buildtype=release',
    'optimization=3',        # -O3 / /O2 (never -Os / -O2 / size)
    'debug=false',
    'b_ndebug=true',
    'b_lto=true',            # IPO / LTO for our code AND the meson subprojects
    'b_lto_mode=thin',       # clang/clang-cl only; ignored by gcc and MSVC
    'b_staticpic=true',      # -fPIC for every static lib
   # 'b_pie=true',            # -fPIE on Linux (Windows is always relocatable/ASLR)
    'b_vscrt=mt',            # static MSVC runtime (/MT) -> no vcruntime DLLs
    'default_library=static',
    'strip=true',
    'unity=off',
  ]
)

cpp       = meson.get_compiler('cpp')
sys       = host_machine.system()
cpu       = host_machine.cpu_family()
cpp_id    = cpp.get_id()
syntax    = cpp.get_argument_syntax()
linker_id = cpp.get_linker_id()
is_lld    = linker_id in ['ld.lld', 'lld-link']

# Toolchain priority
#   Windows : clang-cl  >  cl (MSVC)  >  clang / gcc (MinGW-style)
#   Linux   : clang     >  gcc
# Meson cannot choose the compiler from inside meson.build -- use the native
# files in ./native/ (or CC/CXX env vars) to select it. See notes.
if sys == 'windows'
  if not (cpp_id in ['clang-cl', 'msvc', 'clang', 'gcc'])
    error('Unsupported Windows compiler: ' + cpp_id)
  endif
elif sys == 'linux'
  if not (cpp_id in ['clang', 'gcc'])
    error('Unsupported Linux compiler: ' + cpp_id + ' (use clang or gcc)')
  endif
else
  error('Unsupported host OS: ' + sys + ' (only Windows and Linux are supported)')
endif

# ==============================================================================
# Flag sets
#
#   base_args  every C/C++ TU: ours, meson subprojects (imgui) and the
#              CMake-built deps (SDL3, FreeType). Must be safe for 3rd-party code.
#   cxx_args   same scope, C++ only.
#   math_args  fast-math. Applied to our code + meson subprojects ONLY.
#              Deliberately NOT given to SDL/FreeType, which rely on NaN/Inf
#              semantics in places.
#   link_args  final-link flags.
# ==============================================================================
base_args = []
cxx_args  = []
math_args = []
link_args = []

if cpp_id == 'clang-cl'
  # ---------------------------------------------------------
  # Windows: clang-cl (LLVM backend, MSVC driver)  [PRIORITY 1]
  # ---------------------------------------------------------
  if not is_lld
    warning('clang-cl + ThinLTO needs lld-link (and llvm-lib). Use native/windows-clang-cl.ini.')
  endif

  if cpu == 'x86_64'
    base_args += '/clang:-march=x86-64-v2'
  endif
  base_args += [
    '/clang:-O3',
    '/clang:-finline-functions',
    '/clang:-fvectorize',
    '/clang:-fslp-vectorize',
    '/clang:-funroll-loops',
    '/clang:-fstrict-aliasing',
    '/clang:-fomit-frame-pointer',
    '/clang:-fno-math-errno',
    '/clang:-fno-trapping-math',
    '/GS-',          # no stack cookies
    '/Gw',           # optimize global data (COMDAT per variable)
    '/Gy',           # function-level linking (COMDAT per function)
    '/Zc:inline',    # drop unreferenced COMDATs
  ]
  if cpp.has_multi_arguments(['/clang:-mllvm', '/clang:-inline-threshold=500'])
    base_args += ['/clang:-mllvm', '/clang:-inline-threshold=500']
  endif

  math_args += '/fp:fast'

  link_args += ['/OPT:REF', '/OPT:ICF', '/INCREMENTAL:NO']
  if is_lld
    link_args += ['/opt:lldlto=3', '/mllvm:-inline-threshold=500']
  endif

elif syntax == 'msvc'
  # ---------------------------------------------------------
  # Windows: MSVC cl.exe  [PRIORITY 2]
  # ---------------------------------------------------------
  # cl has no "x86-64-v2" switch; /arch:SSE4.2 is the closest equivalent
  # (needs VS 2022 17.13+; older cl stays on the SSE2 baseline).
  if cpu == 'x86_64'
    if cpp.has_argument('/arch:SSE4.2')
      base_args += '/arch:SSE4.2'
    else
      warning('This cl.exe has no /arch:SSE4.2; falling back to the SSE2 baseline.')
    endif
  endif
  base_args += [
    '/Ob3',          # most aggressive inlining
    '/Ot',           # favor speed over size
    '/Oi',           # intrinsics
    '/GS-',          # no stack cookies
    '/Gw',
    '/Gy',
    '/Zc:inline',
  ]
  # NOTE: /Qpar is intentionally gone. The auto-parallelizer pulls in the
  # vcomp140.dll OpenMP runtime, which breaks "copy the folder anywhere".
  # Loop auto-vectorization is already on at /O2.
  math_args += '/fp:fast'

  # b_lto=true already adds /GL and /LTCG.
  link_args += ['/OPT:REF', '/OPT:ICF', '/INCREMENTAL:NO', '/CGTHREADS:8']

elif syntax == 'gcc'
  # ---------------------------------------------------------
  # clang / gcc: Windows (MinGW-style or clang-msvc-target) and Linux
  # ---------------------------------------------------------
  if sys == 'linux'
    base_args += ['-march=native', '-mtune=native']
  elif cpu == 'x86_64'
    base_args += '-march=x86-64-v2'
  endif

  base_args += cpp.get_supported_arguments(
    '-ftree-vectorize',                 # loop + SLP vectorization (gcc and clang)
    '-funroll-loops',
    '-fstrict-aliasing',
    '-fomit-frame-pointer',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-fno-semantic-interposition',
    '-falign-functions=32',
    '-falign-loops=32',
    '-ffunction-sections',              # one section per symbol so the linker
    '-fdata-sections',                  # can garbage-collect dead code
    '-fno-stack-protector',
    '-fno-stack-clash-protection',
    '-fcf-protection=none',             # no endbr64 / CET overhead
    '-U_FORTIFY_SOURCE',
  )

  if cpp_id == 'gcc'
    base_args += cpp.get_supported_arguments(
      '-fipa-pta',
      '-fno-fat-lto-objects',
    )
  elif cpp_id == 'clang'
    base_args += ['-fvectorize', '-fslp-vectorize', '-finline-functions']
    # Raise clang's inliner threshold (default 225 at -O3).
    if cpp.has_multi_arguments(['-mllvm', '-inline-threshold=500'])
      base_args += ['-mllvm', '-inline-threshold=500']
    endif
  endif

  if sys == 'linux'
    base_args += cpp.get_supported_arguments('-fno-plt')   # direct GOT calls
  endif

  cxx_args  += cpp.get_supported_arguments('-fvisibility-inlines-hidden')
  math_args += '-ffast-math'

  # ---- linker ----
  # ThinLTO objects need lld (or the LLVMgold plugin, which is rarely present).
  if cpp_id == 'clang'
      if cpp.has_link_argument('-fuse-ld=lld')
      link_args += '-fuse-ld=lld'
      is_lld = true
    else
      error('clang + ThinLTO needs lld. Install lld, or select it with native/*.ini.')
    endif
  endif

  if linker_id in ['link', 'lld-link']
    # clang targeting the MSVC ABI: clang++ is the link driver, so forward
    # lld-link options through -Wl,
    link_args += ['-Wl,/OPT:REF', '-Wl,/OPT:ICF', '-Wl,/INCREMENTAL:NO',
                  '-Wl,/opt:lldlto=3', '-Wl,/mllvm:-inline-threshold=500']
  else
    link_args += ['-Wl,--gc-sections', '-Wl,--as-needed', '-s']
    if sys == 'windows'
      # MinGW: no libgcc / libstdc++ / winpthread DLLs next to the exe
      link_args += ['-static', '-static-libgcc']
      link_args += cpp.get_supported_link_arguments('-static-libstdc++')
    else
      link_args += cpp.get_supported_link_arguments(
        '-Wl,-O2',
        '-Wl,--hash-style=gnu',
        '-Wl,--build-id=none',
      )
    endif
  endif

  if is_lld and not (linker_id in ['link', 'lld-link'])
      link_args += cpp.get_supported_link_arguments(
      '-Wl,--icf=all',                       # fold identical functions
      '-Wl,--lto-O3',                        # max optimization in the LTO backend
      '-Wl,--mllvm=-inline-threshold=500',
    )
  endif

else
  error('Unsupported compiler syntax: ' + syntax)
endif

# add_global_* (NOT add_project_*): project arguments are NOT inherited by
# subprojects, so imgui would have been built without any of these flags.
add_global_arguments(base_args + math_args, language : ['c', 'cpp'])
if cxx_args.length() > 0
  add_global_arguments(cxx_args, language : 'cpp')
endif
add_global_link_arguments(link_args, language : ['c', 'cpp'])

# ==============================================================================
# Dependencies
# ==============================================================================
imgui_proj = subproject('imgui')
fleet_deps = [imgui_proj.get_variable('imgui_dep')]

cmake = import('cmake')

# ---- Settings shared by every CMake-built dependency ------------------------
dep_c_flags   = ' '.join(base_args)
dep_cxx_flags = ' '.join(base_args + cxx_args)

if syntax == 'msvc'
  dep_release_flags = (cpp_id == 'clang-cl') ? '/O2 /clang:-O3 /DNDEBUG' : '/O2 /Ob3 /DNDEBUG'
else
  dep_release_flags = '-O3 -DNDEBUG'
endif

cmake_dep_defs = {
  'CMAKE_BUILD_TYPE': 'Release',
  'CMAKE_POLICY_DEFAULT_CMP0069': 'NEW',            # honor INTERPROCEDURAL_OPTIMIZATION
  'CMAKE_POLICY_DEFAULT_CMP0091': 'NEW',            # honor MSVC_RUNTIME_LIBRARY
  'CMAKE_INTERPROCEDURAL_OPTIMIZATION': true,       # IPO/LTO inside the deps too
  'CMAKE_POSITION_INDEPENDENT_CODE': true,
  'BUILD_SHARED_LIBS': false,
  'CMAKE_C_FLAGS': dep_c_flags,
  'CMAKE_CXX_FLAGS': dep_cxx_flags,
  'CMAKE_C_FLAGS_RELEASE': dep_release_flags,       # replaces CMake's own /Ob2 etc.
  'CMAKE_CXX_FLAGS_RELEASE': dep_release_flags,
}

# ---- Windows: static MSVC runtime in the deps as well -----------------------
crt_defs     = {}
sdl_crt_defs = {}
if sys == 'windows'
  if syntax == 'msvc'
    crt_defs     = {'CMAKE_MSVC_RUNTIME_LIBRARY': 'MultiThreaded'}
    sdl_crt_defs = {'SDL_FORCE_STATIC_VCRT': true}
    if cpp_id == 'clang-cl'
      # ThinLTO bitcode in .lib files needs llvm-lib; lib.exe can't index it.
      llvm_lib = find_program('llvm-lib', required : false)
      if llvm_lib.found()
        crt_defs += {'CMAKE_AR': llvm_lib.full_path()}
      endif
    endif
  elif cpp_id == 'clang' and linker_id in ['link', 'lld-link']
    crt_defs     = {
      'CMAKE_MSVC_RUNTIME_LIBRARY': 'MultiThreaded',
      'CMAKE_C_FLAGS': dep_c_flags + ' -fms-runtime-lib=static',
      'CMAKE_CXX_FLAGS': dep_cxx_flags + ' -fms-runtime-lib=static',
    }
    sdl_crt_defs = {'SDL_FORCE_STATIC_VCRT': true}
  endif
endif

# ---- FreeType ---------------------------------------------------------------
ft_opts = cmake.subproject_options()
ft_opts.add_cmake_defines(cmake_dep_defs + crt_defs)
ft_opts.add_cmake_defines({
  'FT_DISABLE_HARFBUZZ': true,
  'FT_DISABLE_PNG': true,
  'FT_DISABLE_BZIP2': true,
  'FT_DISABLE_ZLIB': true,
  'FT_DISABLE_BROTLI': true,
})
ft_proj = cmake.subproject('freetype', options : ft_opts)
fleet_deps += ft_proj.dependency('freetype')

# ---- SDL3 -------------------------------------------------------------------
sdl_opts = cmake.subproject_options()
sdl_opts.add_cmake_defines(cmake_dep_defs + crt_defs + sdl_crt_defs)
sdl_opts.add_cmake_defines({
  'SDL_SHARED': false,
  'SDL_STATIC': true,
  'SDL_ASSERTIONS': 'disabled',
  # Subsystems this app doesn't use:
  'SDL_GPU': false,
  'SDL_AUDIO': false,
  'SDL_CAMERA': false,
  'SDL_JOYSTICK': false,
  'SDL_HAPTIC': false,
  'SDL_HIDAPI': false,
  'SDL_SENSOR': false,
  'SDL_POWER': false,
  'SDL_DIALOG': false,
  # Not needed for a shipping build:
  'SDL_TESTS': false,
  'SDL_TEST_LIBRARY': false,   # SDL3_test.a is built by default even with SDL_TESTS=OFF
  'SDL_EXAMPLES': false,
})
sdl_proj = cmake.subproject('sdl', options : sdl_opts)
fleet_deps += sdl_proj.dependency('SDL3-static')

# ==============================================================================
# Executable Target
# ==============================================================================
src_files = files(
  'main.cpp',
  'base_arena.cpp',
  'world.cpp',
  'simulation.cpp',
  'renderer.cpp',
  'app.cpp',
)

executable(
  'fleet_sim',
  src_files,
  dependencies : fleet_deps,
  win_subsystem : 'windows',
  pie : (sys == 'linux'),      # <-- add this line
  install : true,
)
