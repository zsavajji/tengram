# Telegram calls for BlackBerry 10: tg_owt (Telegram's WebRTC) and tgcalls, as static libraries.
# Built by scripts/build-calls.sh inside tengram-build; sources pinned there, QNX patches in patches/tg_owt and
# patches/tgcalls. The source list is Telegram Desktop's (Telegram/cmake/lib_tgcalls.cmake) without desktop capture,
# group calls (they need ffmpeg) and the other platforms; ours is calls/QnxInterface.cpp.
cmake_minimum_required(VERSION 3.16)
project(tengram_calls LANGUAGES C CXX ASM)

set(TG_OWT_SPECIAL_TARGET qnx CACHE STRING "")
add_subdirectory(${TENGRAM_THIRD_PARTY}/tg_owt tg_owt)

set(tgcalls_loc ${TENGRAM_THIRD_PARTY}/tgcalls/tgcalls)
add_library(tgcalls STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/QnxInterface.cpp
)
foreach(f
    Instance.cpp AudioDeviceHelper.cpp ChannelManager.cpp CodecSelectHelper.cpp CryptoHelper.cpp
    EncryptedConnection.cpp FakeAudioDeviceModule.cpp FieldTrialsConfig.cpp InstanceImpl.cpp LogSinkImpl.cpp
    Manager.cpp MediaManager.cpp Message.cpp NetworkManager.cpp SctpDataChannelProviderInterfaceImpl.cpp
    StaticThreads.cpp TurnCustomizerImpl.cpp VideoCaptureInterface.cpp VideoCaptureInterfaceImpl.cpp
    utils/gzip.cpp third-party/json11.cpp
    v2/ContentNegotiation.cpp v2/CustomDcSctpSocket.cpp v2/DirectNetworkingImpl.cpp
    v2/ExternalSignalingConnection.cpp v2/InstanceV2ReferenceImpl.cpp v2/InstanceV2Impl.cpp
    v2/MtProtoIceTransport.cpp v2/NativeNetworkingImpl.cpp v2/RawTcpSocket.cpp v2/ReflectorPort.cpp
    v2/ReflectorRelayPortFactory.cpp v2/Signaling.cpp v2/SignalingConnection.cpp v2/SignalingEncryption.cpp
    v2/SignalingSctpConnection.cpp)
    target_sources(tgcalls PRIVATE ${tgcalls_loc}/${f})
endforeach()
target_include_directories(tgcalls PUBLIC ${TENGRAM_THIRD_PARTY}/tgcalls PRIVATE ${tgcalls_loc}
    ${TG_OWT_OPENSSL_INCLUDE_PATH})
target_compile_definitions(tgcalls PUBLIC TGCALLS_USE_STD_OPTIONAL PRIVATE RTC_ENABLE_H265 RTC_ENABLE_VP9)
target_compile_options(tgcalls PRIVATE -Wno-deprecated-volatile -Wno-ambiguous-reversed-operator
    -Wno-deprecated-declarations -Wno-unqualified-std-cast-call -Wno-unused-function)
target_link_libraries(tgcalls PUBLIC tg_owt)
