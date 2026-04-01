#include "rxtech/replay_manifest.h"
#include "rxtech/replay_sender.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct ReplayCliArgs {
    std::string unit_dir;
    std::string manifest_path;
    std::string target;
    std::uint32_t limit = 0;
    bool dry_run = false;
};

ReplayCliArgs parse_args(int argc, char** argv) {
    ReplayCliArgs args;
    for (int index = 1; index < argc; ++index) {
        const std::string current = argv[index];
        if (current == "--dry-run") {
            args.dry_run = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::runtime_error("missing value for argument: " + current);
        }
        const std::string value = argv[++index];
        if (current == "--unit-dir") {
            args.unit_dir = value;
        } else if (current == "--manifest") {
            args.manifest_path = value;
        } else if (current == "--target") {
            args.target = value;
        } else if (current == "--limit") {
            args.limit = static_cast<std::uint32_t>(std::stoul(value));
        } else {
            throw std::runtime_error("unknown argument: " + current);
        }
    }

    if (args.unit_dir.empty()) {
        throw std::runtime_error("--unit-dir is required");
    }
    if (args.target.empty()) {
        throw std::runtime_error("--target is required");
    }
    if (args.manifest_path.empty()) {
        args.manifest_path = args.unit_dir + "/cpi_0002_replay_manifest.json";
    }
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const ReplayCliArgs args = parse_args(argc, argv);
        const rxtech::ReplayManifest manifest = rxtech::load_replay_manifest(args.manifest_path);
        const rxtech::ReplayPlan plan = rxtech::build_replay_plan(args.unit_dir, manifest);
        const rxtech::ReplayTarget target = rxtech::parse_replay_target(args.target);

        const std::size_t send_count =
            args.limit == 0U ? plan.datagrams.size() : std::min<std::size_t>(plan.datagrams.size(), args.limit);

        std::cout << "manifest_entries=" << plan.datagrams.size()
                  << " send_count=" << send_count
                  << " target=" << target.host << ":" << target.port
                  << " dry_run=" << (args.dry_run ? "true" : "false")
                  << std::endl;

        if (args.dry_run) {
            return 0;
        }

        const int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            throw std::runtime_error("failed to create UDP socket");
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(target.port);
        if (inet_pton(AF_INET, target.host.c_str(), &addr.sin_addr) != 1) {
            close(socket_fd);
            throw std::runtime_error("invalid target host");
        }

        for (std::size_t index = 0; index < send_count; ++index) {
            const rxtech::ReplayDatagram& datagram = plan.datagrams[index];
            const ssize_t sent = sendto(socket_fd,
                                        datagram.payload.data(),
                                        datagram.payload.size(),
                                        0,
                                        reinterpret_cast<const sockaddr*>(&addr),
                                        sizeof(addr));
            if (sent < 0 || static_cast<std::size_t>(sent) != datagram.payload.size()) {
                close(socket_fd);
                throw std::runtime_error("failed to send replay datagram");
            }
        }

        close(socket_fd);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "replay sender failed: " << ex.what() << std::endl;
        return 1;
    }
}
