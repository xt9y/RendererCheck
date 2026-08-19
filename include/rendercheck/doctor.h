#pragma once
#include <string>
namespace rendercheck {
int run_doctor(bool verbose);
bool vulkan_validation_available(std::string& detail);
}
