import os
import string
import sys


def is_active():
    return True


def get_name():
    return "iOS"


def can_build():

    if sys.platform == 'darwin' or ("OSXCROSS_IOS" in os.environ):
        return True

    return False


def get_opts():
    from SCons.Variables import BoolVariable
    return [
        ('IPHONEPLATFORM', 'Name of the iPhone platform', 'iPhoneOS'),
        ('IPHONEPATH', 'Path to iPhone toolchain', '/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain'),
        ('IPHONESDK', 'Path to the iPhone SDK', '/Applications/Xcode.app/Contents/Developer/Platforms/${IPHONEPLATFORM}.platform/Developer/SDKs/${IPHONEPLATFORM}.sdk/'),
        BoolVariable('game_center', 'Support for game center', True),
        BoolVariable('store_kit', 'Support for in-app store', True),
        BoolVariable('icloud', 'Support for iCloud', True),
        BoolVariable('ios_exceptions', 'Enable exceptions', False),
        ('ios_triple', 'Triple for ios toolchain', ''),
        BoolVariable('ios_sim', 'Build simulator binary (deprecated, use arch=x86 or arch=x86_64)', False),
    ]


def get_flags():

    return [
        ('tools', False),
    ]


def configure(env):

    ## Build type

    if (env["target"].startswith("release")):
        env.Append(CPPFLAGS=['-DNDEBUG', '-DNS_BLOCK_ASSERTIONS=1'])
        env.Append(CPPFLAGS=['-O2', '-ftree-vectorize', '-fomit-frame-pointer'])
        env.Append(LINKFLAGS=['-O2'])

        if env["target"] == "release_debug":
            env.Append(CPPFLAGS=['-DDEBUG_ENABLED'])

    elif (env["target"] == "debug"):
        env.Append(CPPFLAGS=['-D_DEBUG', '-DDEBUG=1', '-gdwarf-2', '-O0', '-DDEBUG_ENABLED', '-DDEBUG_MEMORY_ENABLED'])

    if (env["use_lto"]):
        env.Append(CPPFLAGS=['-flto'])
        env.Append(LINKFLAGS=['-flto'])

    ## Architecture
    if env["ios_sim"] and not ("arch" in env):
      env["arch"] = "x86"

    if env["arch"] == "x86":  # i386, simulator
        env["bits"] = "32"
    elif env["arch"] == "x86_64":
        env["bits"] = "64"
    elif (env["arch"] == "arm" or env["arch"] == "arm32" or env["arch"] == "armv7" or env["bits"] == "32"):  # arm
        env["arch"] = "arm"
        env["bits"] = "32"
    else:  # armv64
        env["arch"] = "arm64"
        env["bits"] = "64"

    ## Compiler configuration

    env['ENV']['PATH'] = env['IPHONEPATH'] + "/Developer/usr/bin/:" + env['ENV']['PATH']

    compiler_path = '$IPHONEPATH/usr/bin/${ios_triple}'
    s_compiler_path = '$IPHONEPATH/Developer/usr/bin/'

    ccache_path = os.environ.get("CCACHE")
    if ccache_path == None:
        env['CC'] = compiler_path + 'clang'
        env['CXX'] = compiler_path + 'clang++'
        env['S_compiler'] = s_compiler_path + 'gcc'
    else:
        # there aren't any ccache wrappers available for iOS,
        # to enable caching we need to prepend the path to the ccache binary
        env['CC'] = ccache_path + ' ' + compiler_path + 'clang'
        env['CXX'] = ccache_path + ' ' + compiler_path + 'clang++'
        env['S_compiler'] = ccache_path + ' ' + s_compiler_path + 'gcc'
    env['AR'] = compiler_path + 'ar'
    env['RANLIB'] = compiler_path + 'ranlib'

    ## Compile flags

    if (env["arch"] == "x86" or env["arch"] == "x86_64"):
        env['IPHONEPLATFORM'] = 'iPhoneSimulator'
        env['ENV']['MACOSX_DEPLOYMENT_TARGET'] = '10.9'
        arch_flag = "i386" if env["arch"] == "x86" else env["arch"]
        env.Append(CCFLAGS=('-arch ' + arch_flag + ' -fobjc-abi-version=2 -fobjc-legacy-dispatch -fmessage-length=0 -fpascal-strings -fblocks -fasm-blocks -isysroot $IPHONESDK -mios-simulator-version-min=9.0 -DCUSTOM_MATRIX_TRANSFORM_H=\\\"build/iphone/matrix4_iphone.h\\\" -DCUSTOM_VECTOR3_TRANSFORM_H=\\\"build/iphone/vector3_iphone.h\\\"').split())
    elif (env["arch"] == "arm"):
        env.Append(CCFLAGS='-fno-objc-arc -arch armv7 -fmessage-length=0 -fno-strict-aliasing -fdiagnostics-print-source-range-info -fdiagnostics-show-category=id -fdiagnostics-parseable-fixits -fpascal-strings -fblocks -isysroot $IPHONESDK -fvisibility=hidden -mthumb "-DIBOutlet=__attribute__((iboutlet))" "-DIBOutletCollection(ClassName)=__attribute__((iboutletcollection(ClassName)))" "-DIBAction=void)__attribute__((ibaction)" -miphoneos-version-min=9.0 -MMD -MT dependencies'.split())
    elif (env["arch"] == "arm64"):
        env.Append(CCFLAGS='-fno-objc-arc -arch arm64 -fmessage-length=0 -fno-strict-aliasing -fdiagnostics-print-source-range-info -fdiagnostics-show-category=id -fdiagnostics-parseable-fixits -fpascal-strings -fblocks -fvisibility=hidden -MMD -MT dependencies -miphoneos-version-min=9.0 -isysroot $IPHONESDK'.split())
        env.Append(CPPFLAGS=['-DNEED_LONG_INT'])
        env.Append(CPPFLAGS=['-DLIBYUV_DISABLE_NEON'])

    if env['ios_exceptions']:
        env.Append(CPPFLAGS=['-fexceptions'])
    else:
        env.Append(CPPFLAGS=['-fno-exceptions'])

    ## Link flags

    if (env["arch"] == "x86" or env["arch"] == "x86_64"):
        arch_flag = "i386" if env["arch"] == "x86" else env["arch"]
        env.Append(LINKFLAGS=['-arch', arch_flag, '-mios-simulator-version-min=9.0',
                              '-isysroot', '$IPHONESDK',
                              '-Xlinker',
                              '-objc_abi_version',
                              '-Xlinker', '2',
                              '-F$IPHONESDK',
                              ])
    elif (env["arch"] == "arm"):
        env.Append(LINKFLAGS=['-arch', 'armv7', '-Wl,-dead_strip', '-miphoneos-version-min=9.0'])
    if (env["arch"] == "arm64"):
        env.Append(LINKFLAGS=['-arch', 'arm64', '-Wl,-dead_strip', '-miphoneos-version-min=9.0'])

    env.Append(LINKFLAGS=['-isysroot', '$IPHONESDK',
                          '-framework', 'AudioToolbox',
                          '-framework', 'AVFoundation',
                          '-framework', 'CoreAudio',
                          '-framework', 'CoreGraphics',
                          '-framework', 'CoreMedia',
                          '-framework', 'CoreMotion',
                          '-framework', 'Foundation',
                          '-framework', 'GameController',
                          '-framework', 'MediaPlayer',
                          '-framework', 'OpenGLES',
                          '-framework', 'QuartzCore',
                          '-framework', 'Security',
                          '-framework', 'SystemConfiguration',
                          '-framework', 'UIKit',
                          ])

    # Feature options
    if env['game_center']:
        env.Append(CPPFLAGS=['-DGAME_CENTER_ENABLED'])
        env.Append(LINKFLAGS=['-framework', 'GameKit'])

    if env['store_kit']:
        env.Append(CPPFLAGS=['-DSTOREKIT_ENABLED'])
        env.Append(LINKFLAGS=['-framework', 'StoreKit'])

    if env['icloud']:
        env.Append(CPPFLAGS=['-DICLOUD_ENABLED'])

    env.Append(CPPPATH=['$IPHONESDK/usr/include',
                        '$IPHONESDK/System/Library/Frameworks/OpenGLES.framework/Headers',
                        '$IPHONESDK/System/Library/Frameworks/AudioUnit.framework/Headers',
                        ])

    env['ENV']['CODESIGN_ALLOCATE'] = '/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/codesign_allocate'

    env.Append(CPPPATH=['#platform/iphone'])
    env.Append(CPPFLAGS=['-DIPHONE_ENABLED', '-DUNIX_ENABLED', '-DGLES_ENABLED', '-DCOREAUDIO_ENABLED'])

    # TODO: Move that to opus module's config
    if 'module_opus_enabled' in env and env['module_opus_enabled']:
        env.opus_fixed_point = "yes"
        if (env["arch"] == "arm"):
            env.Append(CFLAGS=["-DOPUS_ARM_OPT"])
        elif (env["arch"] == "arm64"):
            env.Append(CFLAGS=["-DOPUS_ARM64_OPT"])
