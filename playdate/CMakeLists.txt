cmake_minimum_required(VERSION 3.19)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(ENVSDK $ENV{PLAYDATE_SDK_PATH})
if (NOT "${ENVSDK}" STREQUAL "")
  file(TO_CMAKE_PATH "${ENVSDK}" SDK)
else()
  set(SDK "/home/antigravity/PlaydateSDK")
endif()

project(CrossPoint C CXX ASM)

set(PLAYDATE_GAME_NAME CrossPoint)
set(PLAYDATE_GAME_DEVICE CrossPoint_DEVICE)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Generate I18n tables if not yet present
if(NOT EXISTS "${REPO_ROOT}/lib/I18n/I18nTables.cpp")
  execute_process(
    COMMAND ${Python3_EXECUTABLE} "${REPO_ROOT}/scripts/gen_i18n.py"
    WORKING_DIRECTORY "${REPO_ROOT}"
    RESULT_VARIABLE I18N_RES
  )
endif()

include(FetchContent)

FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  SOURCE_SUBDIR _nonexistent_cmake_subdir
)
FetchContent_Declare(
  qrcode
  GIT_REPOSITORY https://github.com/ricmoo/QRCode.git
  GIT_TAG v0.0.1
  SOURCE_SUBDIR _nonexistent_cmake_subdir
)
FetchContent_Declare(
  pngdec
  GIT_REPOSITORY https://github.com/bitbank2/PNGdec.git
  GIT_TAG 1.1.6
  SOURCE_SUBDIR _nonexistent_cmake_subdir
)
FetchContent_Declare(
  jpegdec
  GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
  GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b
  SOURCE_SUBDIR _nonexistent_cmake_subdir
)
FetchContent_Declare(
  animatedgif
  GIT_REPOSITORY https://github.com/bitbank2/AnimatedGIF.git
  GIT_TAG 67cfacafe12ef4926c7713342c57d2a4af040e5e
  SOURCE_SUBDIR _nonexistent_cmake_subdir
)

FetchContent_MakeAvailable(arduinojson qrcode pngdec jpegdec animatedgif)

# Apply CrossPoint's JPEGDEC and AnimatedGIF safety/bounds patches
execute_process(
  COMMAND ${Python3_EXECUTABLE} "${REPO_ROOT}/scripts/patch_animatedgif.py" "${animatedgif_SOURCE_DIR}"
  RESULT_VARIABLE GIF_PATCH_RES
)
if(NOT GIF_PATCH_RES EQUAL 0)
  message(FATAL_ERROR "AnimatedGIF dependency patch failed")
endif()

execute_process(
  COMMAND ${Python3_EXECUTABLE} -c "
import glob, os, pathlib, subprocess
jpeg_dir = '${jpegdec_SOURCE_DIR}'
patch_dir = '${REPO_ROOT}/scripts/jpegdec_patches'
for patch in sorted(glob.glob(os.path.join(patch_dir, '*.patch'))):
    rev = subprocess.run(['git', 'apply', '--check', '--reverse', patch], cwd=jpeg_dir, capture_output=True)
    if rev.returncode != 0:
        chk = subprocess.run(['git', 'apply', '--check', patch], cwd=jpeg_dir, capture_output=True)
        if chk.returncode == 0:
            subprocess.run(['git', 'apply', patch], cwd=jpeg_dir, check=True)
p = pathlib.Path(jpeg_dir) / 'src' / 'jpeg.inl'
if p.exists():
    txt = p.read_text()
    txt = txt.replace('if (iWidth > MAX_WIDTH || iHeight > MAX_HEIGHT)', 'if (0)')
    p.write_text(txt)
"
)

set_source_files_properties(
  "${animatedgif_SOURCE_DIR}/src/AnimatedGIF.cpp"
  PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/include/Arduino.h"
)

file(GLOB PLAYDATE_HAL_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE APP_SRCS
  "${REPO_ROOT}/src/*.cpp"
  "${REPO_ROOT}/src/*.c"
)
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/main\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/platform/.*")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/network/CrossPointWebServer\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/network/WebDAVHandler\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/network/OtaUpdater\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/network/OtaBootSwitch\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/network/FirmwareFlasher\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/activities/network/CrossPointWebServerActivity\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/activities/network/CalibreConnectActivity\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/activities/network/UsbDriveActivity\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/activities/network/WifiSelectionActivity\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/activities/settings/OtaUpdateActivity\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/activities/settings/SdFirmwareUpdateActivity\\.cpp$")
list(FILTER APP_SRCS EXCLUDE REGEX ".*/src/activities/settings/BleSettingsActivity\\.cpp$")

file(GLOB_RECURSE LIB_SRCS
  "${REPO_ROOT}/lib/*.cpp"
  "${REPO_ROOT}/lib/*.c"
)
list(FILTER LIB_SRCS EXCLUDE REGEX ".*/lib/hal/.*")
list(FILTER LIB_SRCS EXCLUDE REGEX ".*/lib/TrustedTime/.*")
list(FILTER LIB_SRCS EXCLUDE REGEX ".*/lib/DeviceSecret/.*")
list(FILTER LIB_SRCS EXCLUDE REGEX ".*/lib/Epub/BookKey\\.cpp$")
list(FILTER LIB_SRCS EXCLUDE REGEX ".*/lib/miniz/third_party/.*")
list(FILTER LIB_SRCS EXCLUDE REGEX ".*/test/.*")
list(FILTER LIB_SRCS EXCLUDE REGEX ".*/scripts/.*")

file(GLOB_RECURSE SDK_SRCS
  "${REPO_ROOT}/freeink-sdk/libs/assets/ImageToneMapper/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/assets/ProgressiveJpeg/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/book/ContentProtection/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/src/*.c"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/BoardConfig/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/MemoryManager/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/network/CatalogList/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/network/Opds/src/*.cpp"
  "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/*.cpp"
)

file(GLOB PNGDEC_C_SRCS
  "${pngdec_SOURCE_DIR}/src/*.c"
)

set(THIRD_PARTY_SRCS
  "${qrcode_SOURCE_DIR}/src/qrcode.c"
  "${pngdec_SOURCE_DIR}/src/PNGdec.cpp"
  ${PNGDEC_C_SRCS}
  "${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp"
  "${animatedgif_SOURCE_DIR}/src/AnimatedGIF.cpp"
)

set(ALL_CROSSPOINT_SRCS
  ${PLAYDATE_HAL_SRCS}
  ${APP_SRCS}
  ${LIB_SRCS}
  ${SDK_SRCS}
  ${THIRD_PARTY_SRCS}
)

set(ALL_CROSSPOINT_INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${REPO_ROOT}/src"
  "${REPO_ROOT}/lib"
  "${REPO_ROOT}/lib/AsciiText"
  "${REPO_ROOT}/lib/BlePageTurner/include"
  "${REPO_ROOT}/lib/BmpToBmpConverter"
  "${REPO_ROOT}/lib/DeviceSecret"
  "${REPO_ROOT}/lib/EpdFont"
  "${REPO_ROOT}/lib/EpdFont/builtinFonts"
  "${REPO_ROOT}/lib/Epub"
  "${REPO_ROOT}/lib/Epub/Epub"
  "${REPO_ROOT}/lib/Epub/Epub/blocks"
  "${REPO_ROOT}/lib/Epub/Epub/converters"
  "${REPO_ROOT}/lib/Epub/Epub/css"
  "${REPO_ROOT}/lib/Epub/Epub/hyphenation"
  "${REPO_ROOT}/lib/Epub/Epub/parsers"
  "${REPO_ROOT}/lib/FsHelpers"
  "${REPO_ROOT}/lib/GfxRenderer"
  "${REPO_ROOT}/lib/GifSupport"
  "${REPO_ROOT}/lib/GifToBmpConverter"
  "${REPO_ROOT}/lib/HttpHeader"
  "${REPO_ROOT}/lib/I18n"
  "${REPO_ROOT}/lib/InflateReader"
  "${REPO_ROOT}/lib/JpegToBmpConverter"
  "${REPO_ROOT}/lib/JsonParser"
  "${REPO_ROOT}/lib/KOReaderSync"
  "${REPO_ROOT}/lib/LibraryIndex"
  "${REPO_ROOT}/lib/Logging"
  "${REPO_ROOT}/lib/Memory"
  "${REPO_ROOT}/lib/MiniBidi"
  "${REPO_ROOT}/lib/PngToBmpConverter"
  "${REPO_ROOT}/lib/Serialization"
  "${REPO_ROOT}/lib/TrustedTime"
  "${REPO_ROOT}/lib/Txt"
  "${REPO_ROOT}/lib/Utf8"
  "${REPO_ROOT}/lib/XmlParserUtils"
  "${REPO_ROOT}/lib/Xtc"
  "${REPO_ROOT}/lib/Xtc/Xtc"
  "${REPO_ROOT}/lib/ZipFile"
  "${REPO_ROOT}/lib/expat"
  "${REPO_ROOT}/lib/hal"
  "${REPO_ROOT}/lib/miniz"
  "${REPO_ROOT}/lib/miniz/src"
  "${REPO_ROOT}/lib/uzlib"
  "${REPO_ROOT}/lib/uzlib/src"
  "${REPO_ROOT}/freeink-sdk/libs/assets/Icons/include"
  "${REPO_ROOT}/freeink-sdk/libs/assets/ImageToneMapper/include"
  "${REPO_ROOT}/freeink-sdk/libs/assets/ProgressiveJpeg/include"
  "${REPO_ROOT}/freeink-sdk/libs/book/ContentProtection/include"
  "${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/include"
  "${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include"
  "${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/third_party/stb"
  "${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/third_party/freetype/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/BatteryMonitor/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/BoardConfig/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/FrontlightManager/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/HapticManager/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/Imu/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/InputManager/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/MemoryManager/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/PowerManager/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/Rtc/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/SDCardManager/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/UsbMassStorage/include"
  "${REPO_ROOT}/freeink-sdk/libs/hardware/XteinkDetect/include"
  "${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost/include"
  "${REPO_ROOT}/freeink-sdk/libs/network/CatalogList/include"
  "${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include"
  "${REPO_ROOT}/freeink-sdk/libs/network/Opds/include"
  "${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include"
  "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include"
  "${arduinojson_SOURCE_DIR}/src"
  "${qrcode_SOURCE_DIR}/src"
  "${pngdec_SOURCE_DIR}/src"
  "${jpegdec_SOURCE_DIR}/src"
  "${animatedgif_SOURCE_DIR}/src"
)

set(ALL_CROSSPOINT_DEFINES
  CROSSPOINT_VERSION="2.0.0-playdate"
  CROSSPOINT_GIT_BRANCH="feat/playdate-port"
  CROSSPOINT_PLAYDATE=1
  CROSSPOINT_CUSTOM_DISPLAY_HAL=1
  CROSSPOINT_DISPLAY_WIDTH=400
  CROSSPOINT_DISPLAY_HEIGHT=240
  CROSSPOINT_DEFAULT_UI_ORIENTATION=GfxRenderer::LandscapeCounterClockwise
  CROSSPOINT_DEFAULT_UI_LANDSCAPE_CCW=1
  CROSSPOINT_COOPERATIVE_RENDER=1
  CROSSPOINT_CAP_LOCAL_TRANSFER=0
  CROSSPOINT_CAP_OTA=0
  FREEINK_CAP_BLE_HID_HOST=0
  FREEINK_CAP_USB_MSC=0
  FREEINK_DEVICE_X4=1
  FREEINK_FONT_ENABLE_AUTOHINT=1
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STREAM=1
  ARDUINOJSON_ENABLE_ARDUINO_PRINT=1
  CONTENT_EXTERNAL_MINIZ=1
  XML_GE=0
  XML_CONTEXT_BYTES=0
  MAX_WIDTH=3072
  PNG_MAX_BUFFERED_PIXELS=16416
)

if (TOOLCHAIN STREQUAL "armgcc")
  add_executable(${PLAYDATE_GAME_DEVICE} ${ALL_CROSSPOINT_SRCS})
  set(ACTIVE_TARGET ${PLAYDATE_GAME_DEVICE})
else()
  add_library(${PLAYDATE_GAME_NAME} SHARED ${ALL_CROSSPOINT_SRCS})
  set(ACTIVE_TARGET ${PLAYDATE_GAME_NAME})
endif()

include(${SDK}/C_API/buildsupport/playdate_game.cmake)

# Suppress Playdate SDK's default CMAKE_VERBOSE_MAKEFILE=TRUE
set(CMAKE_VERBOSE_MAKEFILE FALSE)

target_include_directories(${ACTIVE_TARGET} BEFORE PRIVATE ${ALL_CROSSPOINT_INCLUDES})
target_compile_definitions(${ACTIVE_TARGET} PRIVATE ${ALL_CROSSPOINT_DEFINES})
target_compile_options(${ACTIVE_TARGET} PRIVATE
  -ffunction-sections
  -fdata-sections
  -Wno-double-promotion
  -Wno-unused-parameter
  -Wno-missing-field-initializers
  $<$<COMPILE_LANGUAGE:C>:-Wno-strict-prototypes>
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstddef -include cstdint>"
)
if (NOT TOOLCHAIN STREQUAL "armgcc")
  target_link_options(${ACTIVE_TARGET} PRIVATE -Wl,--gc-sections -Wl,--no-undefined)
endif()
