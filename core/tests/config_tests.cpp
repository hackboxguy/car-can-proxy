#include "Config.h"
#include "test_support.h"
#include <vector>

using namespace canproxy;

static std::string parse(std::vector<const char *> args, Config &cfg)
{
    args.insert(args.begin(), "can-proxyd");
    cfg = Config();
    return Config::parse(static_cast<int>(args.size()), const_cast<char **>(args.data()), cfg);
}

int main()
{
    Config c;
    CHECK(parse({}, c) == "--contract-if is required");
    CHECK(parse({ "--contract-if=vcan0" }, c) == "--plugin is required");
    CHECK(parse({ "--contract-if=vcan0", "--plugin=sim" }, c).empty());
    CHECK(c.contractIf == "vcan0" && c.plugin == "sim" && c.pluginTimeoutMs == 1000);
    CHECK(parse({ "--contract-if", "vcan0", "--plugin", "sim", "--vehicle-if", "vcan1" }, c).empty());
    CHECK(c.vehicleIf == "vcan1");
    CHECK(parse({ "--contract-if=vcan0", "--plugin=sim", "--plugin-arg", "drivetrain=ice", "--plugin-arg=assist=0" }, c).empty());
    CHECK(c.pluginArgs.size() == 2 && c.pluginArgs[0].first == "drivetrain" && c.pluginArgs[0].second == "ice");
    CHECK(c.pluginArgs[1].first == "assist" && c.pluginArgs[1].second == "0");
    CHECK(!parse({ "--contract-if=vcan0", "--plugin=sim", "--plugin-arg=novalue" }, c).empty());
    CHECK(!parse({ "--contract-if=vcan0", "--plugin=sim", "--plugin-timeout-ms=0" }, c).empty());
    CHECK(parse({ "--contract-if=vcan0", "--plugin=sim", "--plugin-timeout-ms=250", "--log-level=debug" }, c).empty());
    CHECK(c.pluginTimeoutMs == 250 && c.logLevel == LogLevel::Debug);
    CHECK(!parse({ "--contract-if=vcan0", "--plugin=sim", "--log-level=loud" }, c).empty());
    CHECK(!parse({ "--contract-if=vcan0", "--plugin=sim", "--bogus" }, c).empty());
    CHECK(parse({ "--help" }, c).empty() && c.help);

    // plugin path resolution
    CHECK(parse({ "--contract-if=vcan0", "--plugin=sim", "--plugin-dir=/opt/p" }, c).empty());
    CHECK(c.resolvePluginPath() == "/opt/p/sim.so");
    CHECK(parse({ "--contract-if=vcan0", "--plugin=/x/y.so" }, c).empty());
    CHECK(c.resolvePluginPath() == "/x/y.so");
    CHECK(parse({ "--contract-if=vcan0", "--plugin=local.so" }, c).empty());
    CHECK(c.resolvePluginPath() == "local.so");
    return REPORT("config_tests");
}
