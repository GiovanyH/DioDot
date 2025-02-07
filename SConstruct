#!/usr/bin/env python

EnsureSConsVersion(0, 98, 1)

# System
import glob
import os
import string
import sys

# Local
import methods

# moved below to compensate with module version string
# methods.update_version()

# scan possible build platforms

platform_list = []  # list of platforms
platform_opts = {}  # options for each platform
platform_flags = {}  # flags for each platform

active_platforms = []
active_platform_ids = []
platform_exporters = []
platform_apis = []
global_defaults = []

for x in glob.glob("platform/*"):
    if (not os.path.isdir(x) or not os.path.exists(x + "/detect.py")):
        continue
    tmppath = "./" + x

    sys.path.insert(0, tmppath)
    import detect

    if (os.path.exists(x + "/export/export.cpp")):
        platform_exporters.append(x[9:])
    if (os.path.exists(x + "/api/api.cpp")):
        platform_apis.append(x[9:])
    if (os.path.exists(x + "/globals/global_defaults.cpp")):
        global_defaults.append(x[9:])
    if (detect.is_active()):
        active_platforms.append(detect.get_name())
        active_platform_ids.append(x)
    if (detect.can_build()):
        x = x.replace("platform/", "")  # rest of world
        x = x.replace("platform\\", "")  # win32
        platform_list += [x]
        platform_opts[x] = detect.get_opts()
        platform_flags[x] = detect.get_flags()
    sys.path.remove(tmppath)
    sys.modules.pop('detect')

module_list = methods.detect_modules()


# print "Detected Platforms: "+str(platform_list)

methods.save_active_platforms(active_platforms, active_platform_ids)

custom_tools = ['default']

platform_arg = ARGUMENTS.get("platform", ARGUMENTS.get("p", False))

if (os.name == "posix"):
    pass
elif (os.name == "nt"):
    if (os.getenv("VCINSTALLDIR") == None or platform_arg == "android" or platform_arg == "javascript"):
        custom_tools = ['mingw']

env_base = Environment(tools=custom_tools)
if 'TERM' in os.environ:
    env_base['ENV']['TERM'] = os.environ['TERM']
env_base.AppendENVPath('PATH', os.getenv('PATH'))
env_base.AppendENVPath('PKG_CONFIG_PATH', os.getenv('PKG_CONFIG_PATH'))
env_base.global_defaults = global_defaults
env_base.android_maven_repos = []
env_base.android_flat_dirs = []
env_base.android_dependencies = []
env_base.android_gradle_plugins = []
env_base.android_gradle_classpath = []
env_base.android_java_dirs = []
env_base.android_res_dirs = []
env_base.android_asset_dirs = []
env_base.android_aidl_dirs = []
env_base.android_jni_dirs = []
env_base.android_default_config = []
env_base.android_manifest_chunk = ""
env_base.android_permission_chunk = ""
env_base.android_appattributes_chunk = ""
env_base.disabled_modules = []
env_base.use_ptrcall = False
env_base.split_drivers = False
env_base.split_modules = False
env_base.module_version_string = ""

# To decide whether to rebuild a file, use the MD5 sum only if the timestamp has changed.
# http://scons.org/doc/production/HTML/scons-user/ch06.html#idm139837621851792
env_base.Decider('MD5-timestamp')
# Use cached implicit dependencies by default. Can be overridden by specifying `--implicit-deps-changed` in the command line.
# http://scons.org/doc/production/HTML/scons-user/ch06s04.html
env_base.SetOption('implicit_cache', 1)


env_base.__class__.android_add_maven_repository = methods.android_add_maven_repository
env_base.__class__.android_add_flat_dir = methods.android_add_flat_dir
env_base.__class__.android_add_dependency = methods.android_add_dependency
env_base.__class__.android_add_java_dir = methods.android_add_java_dir
env_base.__class__.android_add_res_dir = methods.android_add_res_dir
env_base.__class__.android_add_asset_dir = methods.android_add_asset_dir
env_base.__class__.android_add_aidl_dir = methods.android_add_aidl_dir
env_base.__class__.android_add_jni_dir = methods.android_add_jni_dir
env_base.__class__.android_add_default_config = methods.android_add_default_config
env_base.__class__.android_add_to_manifest = methods.android_add_to_manifest
env_base.__class__.android_add_to_permissions = methods.android_add_to_permissions
env_base.__class__.android_add_to_attributes = methods.android_add_to_attributes
env_base.__class__.android_add_gradle_plugin = methods.android_add_gradle_plugin
env_base.__class__.android_add_gradle_classpath = methods.android_add_gradle_classpath
env_base.__class__.disable_module = methods.disable_module

env_base.__class__.add_module_version_string = methods.add_module_version_string

env_base.__class__.add_source_files = methods.add_source_files
env_base.__class__.use_windows_spawn_fix = methods.use_windows_spawn_fix
env_base.__class__.split_lib = methods.split_lib

env_base.__class__.add_shared_library = methods.add_shared_library
env_base.__class__.add_library = methods.add_library
env_base.__class__.add_program = methods.add_program
env_base.__class__.CommandNoCache = methods.CommandNoCache

env_base["x86_libtheora_opt_gcc"] = False
env_base["x86_libtheora_opt_vc"] = False

# Build options

customs = ['custom.py']

profile = ARGUMENTS.get("profile", False)
if profile:
    if os.path.isfile(profile):
        customs.append(profile)
    elif os.path.isfile(profile + ".py"):
        customs.append(profile + ".py")

opts = Variables(customs, ARGUMENTS)

# Target build options
opts.Add('arch', "Platform-dependent architecture (arm/arm64/x86/x64/mips/etc)", '')
opts.Add(EnumVariable('bits', "Target platform bits", 'default', ('default', '32', '64', 'fat')))
opts.Add('p', "Platform (alias for 'platform')", '')
opts.Add('platform', "Target platform (%s)" % ('|'.join(platform_list), ), '')
opts.Add(EnumVariable('target', "Compilation target", 'debug', ('debug', 'release_debug', 'release')))
opts.Add(BoolVariable('tools', "Build the tools a.k.a. the Godot editor", True))
opts.Add(BoolVariable('use_lto', 'Use linking time optimization', False))

# Components
opts.Add(BoolVariable('deprecated', "Enable deprecated features", True))
opts.Add(BoolVariable('gdscript', "Build GDSCript support", True))
opts.Add(BoolVariable('minizip', "Build minizip archive support", True))
opts.Add(BoolVariable('xaudio2', "XAudio2 audio driver", False))
opts.Add(BoolVariable('xml', "XML format support for resources", True))

# Advanced options
opts.Add(BoolVariable('disable_3d', "Disable 3D nodes for smaller executable", False))
opts.Add(BoolVariable('disable_advanced_gui', "Disable advanced 3D gui nodes and behaviors", False))
opts.Add('extra_suffix', "Custom extra suffix added to the base filename of all generated binary files", '')
opts.Add('unix_global_settings_path', "UNIX-specific path to system-wide settings. Currently only used for templates", '')
opts.Add(BoolVariable('verbose', "Enable verbose output for the compilation", False))
opts.Add(BoolVariable('vsproj', "Generate Visual Studio Project", False))
opts.Add(EnumVariable('warnings', "Set the level of warnings emitted during compilation", 'no', ('extra', 'all', 'moderate', 'no')))
opts.Add(BoolVariable('progress', "Show a progress indicator during build", True))
opts.Add(BoolVariable('dev', "If yes, alias for verbose=yes warnings=all", False))
opts.Add(EnumVariable('macports_clang', "Build using clang from MacPorts", 'no', ('no', '5.0', 'devel')))

# Thirdparty libraries
opts.Add(BoolVariable('builtin_bullet', "Use the builtin bullet library", True))
opts.Add(BoolVariable('builtin_enet', "Use the builtin enet library", True))
opts.Add(BoolVariable('builtin_freetype', "Use the builtin freetype library", True))
opts.Add(BoolVariable('builtin_libogg', "Use the builtin libogg library", True))
opts.Add(BoolVariable('builtin_libpng', "Use the builtin libpng library", True))
opts.Add(BoolVariable('builtin_libtheora', "Use the builtin libtheora library", True))
opts.Add(BoolVariable('builtin_libvorbis', "Use the builtin libvorbis library", True))
opts.Add(BoolVariable('builtin_libvpx', "Use the builtin libvpx library", True))
opts.Add(BoolVariable('builtin_libwebp', "Use the builtin libwebp library", True))
opts.Add(BoolVariable('builtin_openssl', "Use the builtin openssl library", True))
opts.Add(BoolVariable('builtin_opus', "Use the builtin opus library", True))
opts.Add(BoolVariable('builtin_pcre2', "Use the builtin pcre2 library)", True))
opts.Add(BoolVariable('builtin_recast', "Use the builtin recast library", True))
opts.Add(BoolVariable('builtin_squish', "Use the builtin squish library", True))
opts.Add(BoolVariable('builtin_thekla_atlas', "Use the builtin thekla_altas library", True))
opts.Add(BoolVariable('builtin_zlib', "Use the builtin zlib library", True))
opts.Add(BoolVariable('builtin_zstd', "Use the builtin zstd library", True))
opts.Add(BoolVariable('no_editor_splash', "Don't use the custom splash screen for the editor", False))

# Compilation environment setup
opts.Add("CXX", "C++ compiler")
opts.Add("CC", "C compiler")
opts.Add("LINK", "Linker")
opts.Add("CCFLAGS", "Custom flags for both the C and C++ compilers")
opts.Add("CXXFLAGS", "Custom flags for the C++ compiler")
opts.Add("CFLAGS", "Custom flags for the C compiler")
opts.Add("LINKFLAGS", "Custom flags for the linker")


# add platform specific options

for k in platform_opts.keys():
    opt_list = platform_opts[k]
    for o in opt_list:
        opts.Add(o)

for x in module_list:
    module_enabled = True
    tmppath = "./modules/" + x
    sys.path.insert(0, tmppath)
    import config
    enabled_attr = getattr(config, "is_enabled", None)
    if (callable(enabled_attr) and not config.is_enabled()):
        module_enabled = False
    sys.path.remove(tmppath)
    sys.modules.pop('config')
    opts.Add(BoolVariable('module_' + x + '_enabled', "Enable module '%s'" % (x, ), module_enabled))

opts.Update(env_base)  # update environment
Help(opts.GenerateHelpText(env_base))  # generate help

# add default include paths

env_base.Append(CPPPATH=['#core', '#core/math', '#editor', '#drivers', '#'])

# configure ENV for platform
env_base.platform_exporters = platform_exporters
env_base.platform_apis = platform_apis

if (env_base['target'] == 'debug'):
    env_base.Append(CPPFLAGS=['-DDEBUG_MEMORY_ALLOC'])
    env_base.Append(CPPFLAGS=['-DSCI_NAMESPACE'])

if (env_base['no_editor_splash']):
    env_base.Append(CPPFLAGS=['-DNO_EDITOR_SPLASH'])

if not env_base['deprecated']:
    env_base.Append(CPPFLAGS=['-DDISABLE_DEPRECATED'])

env_base.platforms = {}


selected_platform = ""

if env_base['platform'] != "":
    selected_platform = env_base['platform']
elif env_base['p'] != "":
    selected_platform = env_base['p']
    env_base["platform"] = selected_platform


if selected_platform in platform_list:
    tmppath = "./platform/" + selected_platform
    sys.path.insert(0, tmppath)
    import detect
    if "create" in dir(detect):
        env = detect.create(env_base)
    else:
        env = env_base.Clone()

    if env['dev']:
        env["warnings"] = "all"
        env['verbose'] = True

    if env['vsproj']:
        env.vs_incs = []
        env.vs_srcs = []

        def AddToVSProject(sources):
            for x in sources:
                if type(x) == type(""):
                    fname = env.File(x).path
                else:
                    fname = env.File(x)[0].path
                pieces = fname.split(".")
                if len(pieces) > 0:
                    basename = pieces[0]
                    basename = basename.replace('\\\\', '/')
                    if os.path.isfile(basename + ".h"):
                        env.vs_incs = env.vs_incs + [basename + ".h"]
                    elif os.path.isfile(basename + ".hpp"):
                        env.vs_incs = env.vs_incs + [basename + ".hpp"]
                    if os.path.isfile(basename + ".c"):
                        env.vs_srcs = env.vs_srcs + [basename + ".c"]
                    elif os.path.isfile(basename + ".cpp"):
                        env.vs_srcs = env.vs_srcs + [basename + ".cpp"]
        env.AddToVSProject = AddToVSProject

    env.extra_suffix = ""

    if env["extra_suffix"] != '':
        env.extra_suffix += '.' + env["extra_suffix"]

    CCFLAGS = env.get('CCFLAGS', '')
    env['CCFLAGS'] = ''

    env.Append(CCFLAGS=str(CCFLAGS).split())

    CFLAGS = env.get('CFLAGS', '')
    env['CFLAGS'] = ''

    env.Append(CFLAGS=str(CFLAGS).split())

    LINKFLAGS = env.get('LINKFLAGS', '')
    env['LINKFLAGS'] = ''

    env.Append(LINKFLAGS=str(LINKFLAGS).split())

    flag_list = platform_flags[selected_platform]
    for f in flag_list:
        if not (f[0] in ARGUMENTS):  # allow command line to override platform flags
            env[f[0]] = f[1]

    # must happen after the flags, so when flags are used by configure, stuff happens (ie, ssl on x11)
    detect.configure(env)

    if (env["warnings"] == 'yes'):
        print("WARNING: warnings=yes is deprecated; assuming warnings=all")

    env.msvc = 0
    if (os.name == "nt" and os.getenv("VCINSTALLDIR") and (platform_arg == "windows" or platform_arg == "uwp")): # MSVC, needs to stand out of course
        env.msvc = 1
        disable_nonessential_warnings = ['/wd4267', '/wd4244', '/wd4305', '/wd4800'] # Truncations, narrowing conversions...
        if (env["warnings"] == 'extra'):
            env.Append(CCFLAGS=['/Wall']) # Implies /W4
        elif (env["warnings"] == 'all' or env["warnings"] == 'yes'):
            env.Append(CCFLAGS=['/W3'] + disable_nonessential_warnings)
        elif (env["warnings"] == 'moderate'):
            # C4244 shouldn't be needed here being a level-3 warning, but it is
            env.Append(CCFLAGS=['/W2'] + disable_nonessential_warnings)
        else: # 'no'
            env.Append(CCFLAGS=['/w'])
        # Set exception handling model to avoid warnings caused by Windows system headers.
        env.Append(CCFLAGS=['/EHsc'])
    else: # Rest of the world
        if (env["warnings"] == 'extra'):
            env.Append(CCFLAGS=['-Wall', '-Wextra'])
        elif (env["warnings"] == 'all' or env["warnings"] == 'yes'):
            env.Append(CCFLAGS=['-Wall'])
        elif (env["warnings"] == 'moderate'):
            env.Append(CCFLAGS=['-Wall', '-Wno-unused'])
        else: # 'no'
            env.Append(CCFLAGS=['-w'])
        env.Append(CCFLAGS=['-Werror=return-type'])

    #env['platform_libsuffix'] = env['LIBSUFFIX']

    suffix = "." + selected_platform

    if (env["target"] == "release"):
        if env["tools"]:
            print("Tools can only be built with targets 'debug' and 'release_debug'.")
            sys.exit(255)
        suffix += ".opt"
        env.Append(CCFLAGS=['-DNDEBUG'])

    elif (env["target"] == "release_debug"):
        if env["tools"]:
            suffix += ".opt.tools"
        else:
            suffix += ".opt.debug"
    else:
        if env["tools"]:
            suffix += ".tools"
        else:
            suffix += ".debug"

    if env["arch"] != "":
        suffix += "." + env["arch"]
    elif (env["bits"] == "32"):
        suffix += ".32"
    elif (env["bits"] == "64"):
        suffix += ".64"
    elif (env["bits"] == "fat"):
        suffix += ".fat"

    suffix += env.extra_suffix

    sys.path.remove(tmppath)
    sys.modules.pop('detect')

    env.module_list = []
    env.doc_class_path = {}

    for x in module_list:
        if not env['module_' + x + '_enabled']:
            continue
        tmppath = "./modules/" + x
        sys.path.insert(0, tmppath)
        env.current_module = x
        import config
        if (config.can_build(selected_platform)):
            config.configure(env)
            env.module_list.append(x)
            try:
                 doc_classes = config.get_doc_classes()
                 doc_path = config.get_doc_path()
                 for c in doc_classes:
                     env.doc_class_path[c] = "modules/" + x + "/" + doc_path
            except:
                pass

        sys.path.remove(tmppath)
        sys.modules.pop('config')

    methods.update_version(env.module_version_string)

    env["PROGSUFFIX"] = suffix + env.module_version_string + env["PROGSUFFIX"]
    env["OBJSUFFIX"] = suffix + env["OBJSUFFIX"]
    env["LIBSUFFIX"] = suffix + env["LIBSUFFIX"]
    env["SHLIBSUFFIX"] = suffix + env["SHLIBSUFFIX"]

    if (env.use_ptrcall):
        env.Append(CPPFLAGS=['-DPTRCALL_ENABLED'])

    # to test 64 bits compiltion
    # env.Append(CPPFLAGS=['-m64'])

    if env['tools']:
        env.Append(CPPFLAGS=['-DTOOLS_ENABLED'])
    if env['disable_3d']:
        env.Append(CPPFLAGS=['-D_3D_DISABLED'])
    if env['gdscript']:
        env.Append(CPPFLAGS=['-DGDSCRIPT_ENABLED'])
    if env['disable_advanced_gui']:
        env.Append(CPPFLAGS=['-DADVANCED_GUI_DISABLED'])

    if env['minizip']:
        env.Append(CPPFLAGS=['-DMINIZIP_ENABLED'])

    if env['xml']:
        env.Append(CPPFLAGS=['-DXML_ENABLED'])

    if not env['verbose']:
        methods.no_verbose(sys, env)

    if (not env["platform"] == "server"): # FIXME: detect GLES3
        env.Append( BUILDERS = { 'GLES3_GLSL' : env.Builder(action = methods.build_gles3_headers, suffix = 'glsl.gen.h',src_suffix = '.glsl') } )

    scons_cache_path = os.environ.get("SCONS_CACHE")
    if scons_cache_path != None:
        CacheDir(scons_cache_path)
        print("Scons cache enabled... (path: '" + scons_cache_path + "')")

    Export('env')

    # build subdirs, the build order is dependent on link order.

    SConscript("core/SCsub")
    SConscript("servers/SCsub")
    SConscript("scene/SCsub")
    SConscript("editor/SCsub")
    SConscript("drivers/SCsub")

    SConscript("platform/SCsub")
    SConscript("modules/SCsub")
    SConscript("main/SCsub")

    SConscript("platform/" + selected_platform + "/SCsub")  # build selected platform

    # Microsoft Visual Studio Project Generation
    if env['vsproj']:
        env['CPPPATH'] = [Dir(path) for path in env['CPPPATH']]
        methods.generate_vs_project(env, GetOption("num_jobs"))
        methods.generate_cpp_hint_file("cpp.hint")

    # Check for the existence of headers
    conf = Configure(env)
    if ("check_c_headers" in env):
        for header in env["check_c_headers"]:
            if (conf.CheckCHeader(header[0])):
                if (env.msvc):
                    env.Append(CCFLAGS=['/D' + header[1]])
                else:
                    env.Append(CCFLAGS=['-D' + header[1]])

else:

    print("No valid target platform selected.")
    print("The following were detected:")
    for x in platform_list:
        print("\t" + x)
    print("\nPlease run scons again with argument: platform=<string>")


# The following only makes sense when the env is defined, and assumes it is
if 'env' in locals():
    screen = sys.stdout
    # Progress reporting is not available in non-TTY environments since it
    # messes with the output (for example, when writing to a file)
    show_progress = (env['progress'] and sys.stdout.isatty())
    node_count = 0
    node_count_max = 0
    node_count_interval = 1
    node_count_fname = str(env.Dir('#')) + '/.scons_node_count'

    import time, math

    class cache_progress:
        # The default is 1 GB cache and 12 hours half life
        def __init__(self, path = None, limit = 1073741824, half_life = 43200):
            self.path = path
            self.limit = limit
            self.exponent_scale = math.log(2) / half_life
            if env['verbose'] and path != None:
                screen.write('Current cache limit is ' + self.convert_size(limit) + ' (used: ' + self.convert_size(self.get_size(path)) + ')\n')
            self.delete(self.file_list())

        def __call__(self, node, *args, **kw):
            global node_count, node_count_max, node_count_interval, node_count_fname, show_progress
            if show_progress:
                # Print the progress percentage
                node_count += node_count_interval
                if (node_count_max > 0 and node_count <= node_count_max):
                    screen.write('\r[%3d%%] ' % (node_count * 100 / node_count_max))
                    screen.flush()
                elif (node_count_max > 0 and node_count > node_count_max):
                    screen.write('\r[100%] ')
                    screen.flush()
                else:
                    screen.write('\r[Initial build] ')
                    screen.flush()

        def delete(self, files):
            if len(files) == 0:
                return
            if env['verbose']:
                # Utter something
                screen.write('\rPurging %d %s from cache...\n' % (len(files), len(files) > 1 and 'files' or 'file'))
            [os.remove(f) for f in files]

        def file_list(self):
            if self.path == None:
                # Nothing to do
                return []
            # Gather a list of (filename, (size, atime)) within the
            # cache directory
            file_stat = [(x, os.stat(x)[6:8]) for x in glob.glob(os.path.join(self.path, '*', '*'))]
            if file_stat == []:
                # Nothing to do
                return []
            # Weight the cache files by size (assumed to be roughly
            # proportional to the recompilation time) times an exponential
            # decay since the ctime, and return a list with the entries
            # (filename, size, weight).
            current_time = time.time()
            file_stat = [(x[0], x[1][0], (current_time - x[1][1])) for x in file_stat]
            # Sort by the most resently accessed files (most sensible to keep) first
            file_stat.sort(key=lambda x: x[2])
            # Search for the first entry where the storage limit is
            # reached
            sum, mark = 0, None
            for i,x in enumerate(file_stat):
                sum += x[1]
                if sum > self.limit:
                    mark = i
                    break
            if mark == None:
                return []
            else:
                return [x[0] for x in file_stat[mark:]]

        def convert_size(self, size_bytes):
            if size_bytes == 0:
                return "0 bytes"
            size_name = ("bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB")
            i = int(math.floor(math.log(size_bytes, 1024)))
            p = math.pow(1024, i)
            s = round(size_bytes / p, 2)
            return "%s %s" % (int(s) if i == 0 else s, size_name[i])

        def get_size(self, start_path = '.'):
            total_size = 0
            for dirpath, dirnames, filenames in os.walk(start_path):
                for f in filenames:
                    fp = os.path.join(dirpath, f)
                    total_size += os.path.getsize(fp)
            return total_size

    def progress_finish(target, source, env):
        global node_count, progressor
        with open(node_count_fname, 'w') as f:
            f.write('%d\n' % node_count)
        progressor.delete(progressor.file_list())

    try:
        with open(node_count_fname) as f:
            node_count_max = int(f.readline())
    except:
        pass

    cache_directory = os.environ.get("SCONS_CACHE")
    # Simple cache pruning, attached to SCons' progress callback. Trim the
    # cache directory to a size not larger than cache_limit.
    cache_limit = float(os.getenv("SCONS_CACHE_LIMIT", 1024)) * 1024 * 1024
    progressor = cache_progress(cache_directory, cache_limit)
    Progress(progressor, interval = node_count_interval)

    progress_finish_command = Command('progress_finish', [], progress_finish)
    AlwaysBuild(progress_finish_command)
