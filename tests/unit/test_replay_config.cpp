#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>

#include "replay_config.h"

int main()
{
    {
        std::array<std::uint8_t, 6> mac{};
        assert(rxtech::replay::parse_mac_address("9c:47:82:e1:36:d3", mac));
        assert(mac[0] == 0x9c);
        assert(mac[1] == 0x47);
        assert(mac[2] == 0x82);
        assert(mac[3] == 0xe1);
        assert(mac[4] == 0x36);
        assert(mac[5] == 0xd3);
    }

    {
        std::array<std::uint8_t, 6> mac{};
        assert(!rxtech::replay::parse_mac_address("9c:47:82:e1:36", mac));
        assert(!rxtech::replay::parse_mac_address("9c-47-82-e1-36-d3", mac));
        assert(!rxtech::replay::parse_mac_address("gg:47:82:e1:36:d3", mac));
    }

    {
        char arg0[] = "rx_replay_sender";
        char arg1[] = "--data-dir";
        char arg2[] = "data/cpi_0002_complete";
        char arg3[] = "--iface";
        char arg4[] = "receiver3";
        char arg5[] = "--src-mac";
        char arg6[] = "9c:47:82:e1:36:d3";
        char arg7[] = "--dst-mac";
        char arg8[] = "9c:47:82:e1:36:d1";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8};

        rxtech::replay::ReplaySenderConfig cfg;
        assert(rxtech::replay::parse_replay_args(9, argv, cfg));
        assert(cfg.src_mac == "9c:47:82:e1:36:d3");
        assert(cfg.dst_mac == "9c:47:82:e1:36:d1");
    }

    {
        char arg0[] = "rx_replay_sender";
        char arg1[] = "--data-dir";
        char arg2[] = "data/cpi_0002_complete";
        char arg3[] = "--iface";
        char arg4[] = "receiver3";
        char arg5[] = "--src-mac";
        char arg6[] = "invalid-mac";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6};

        rxtech::replay::ReplaySenderConfig cfg;
        assert(!rxtech::replay::parse_replay_args(7, argv, cfg));
    }

    return 0;
}
