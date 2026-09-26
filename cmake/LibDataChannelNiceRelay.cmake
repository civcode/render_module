# libdatachannel 0.24.5 filters advertised candidates for TransportPolicy::Relay,
# but does not constrain libnice's actual selected pairs. Apply the native policy
# before gathering. Keep the pinned checkout untouched (including source overrides).
set(ice_source "${libdatachannel_SOURCE_DIR}/src/impl/icetransport.cpp")
file(SHA256 "${ice_source}" ice_hash)
if(NOT ice_hash STREQUAL "58c1a8dfa8ba08f684fa58e165f575cd2489b5f67d318694b8d16ff2d42389da")
    message(FATAL_ERROR "Unexpected libdatachannel ICE source; review the relay-policy patch")
endif()
file(READ "${ice_source}" ice_code)
set(ice_anchor "\tg_object_set(G_OBJECT(mNiceAgent.get()), \"ice-udp\", TRUE, nullptr);")
string(REPLACE "${ice_anchor}"
    "${ice_anchor}\n\t// RenderModule: enforce relay-only selection, not merely candidate filtering.\n\tg_object_set(G_OBJECT(mNiceAgent.get()), \"force-relay\",\n\t             config.iceTransportPolicy == TransportPolicy::Relay ? TRUE : FALSE, nullptr);"
    ice_code "${ice_code}")
# The copy is outside src/impl; disambiguate its internal headers from public
# headers with identical names without changing include order for other sources.
foreach(header icetransport internals transport utils)
    string(REPLACE "#include \"${header}.hpp\"" "#include \"impl/${header}.hpp\"" ice_code "${ice_code}")
endforeach()
set(ice_patched "${CMAKE_CURRENT_BINARY_DIR}/webrtc-patched/icetransport.cpp")
file(CONFIGURE OUTPUT "${ice_patched}" CONTENT "${ice_code}" @ONLY)
foreach(target datachannel datachannel-static)
    get_target_property(sources ${target} SOURCES)
    list(FIND sources "${ice_source}" index)
    if(index LESS 0)
        message(FATAL_ERROR "libdatachannel ICE source layout changed")
    endif()
    list(REMOVE_AT sources ${index})
    list(APPEND sources "${ice_patched}")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
endforeach()
# Supply the modified MPL-covered file with binary installations.
install(FILES "${ice_patched}" DESTINATION "${CMAKE_INSTALL_DATADIR}/render-module/licenses/libdatachannel-modified")
