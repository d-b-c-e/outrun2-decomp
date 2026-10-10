#include "system/exe_image.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace outrun::platform;

// exe_image_map_pe on a synthetic PE (no byte of the original): the two
// accepted layouts (Steam SizeOfImage 590000, retail 58E000) and the bytes
// mapping blanks so both builds give the image the range table expects.
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void wr16(std::vector<std::uint8_t>& f, std::size_t at, std::uint16_t v) { std::memcpy(f.data()+at, &v, 2); }
void wr32(std::vector<std::uint8_t>& f, std::size_t at, std::uint32_t v) { std::memcpy(f.data()+at, &v, 4); }

constexpr std::uint32_t SectionRva = 0x00196000u;   // .rdata onward, as one section
constexpr std::uint32_t SectionRaw = 0x1000u;
constexpr std::uint32_t SectionSize = 0x93000u;
constexpr std::uint8_t Fill = 0xa5u;

std::vector<std::uint8_t> pe(std::uint32_t size_of_image, std::uint32_t image_base = ExeImageBase) {
    std::vector<std::uint8_t> f(SectionRaw+SectionSize, 0);
    f[0] = 'M'; f[1] = 'Z';
    constexpr std::size_t nt = 0x80, opt = nt+24, table = opt+0xE0;
    wr32(f, 0x3C, nt);
    std::memcpy(f.data()+nt, "PE\0\0", 4);
    wr16(f, nt+4, 0x014Cu);   // i386
    wr16(f, nt+6, 1);         // sections
    wr16(f, nt+20, 0xE0);     // optional header size
    wr16(f, opt, 0x010Bu);    // PE32
    wr32(f, opt+28, image_base);
    wr32(f, opt+56, size_of_image);
    wr32(f, table+8, SectionSize);
    wr32(f, table+12, SectionRva);
    wr32(f, table+16, SectionSize);
    wr32(f, table+20, SectionRaw);
    std::memset(f.data()+SectionRaw, Fill, SectionSize);
    return f;
}
std::uint8_t at(const std::vector<std::uint8_t>& image, std::uint32_t va) { return image.at(va-ExeImageBase); }
bool all(const std::vector<std::uint8_t>& image, std::uint32_t va, std::uint32_t size, std::uint8_t value) {
    for (std::uint32_t i = 0; i < size; ++i) if (at(image, va+i) != value) return false;
    return true;
}

void test_retail_layout() {
    std::vector<std::uint8_t> image; std::string error;
    check(exe_image_map_pe(pe(0x0058E000u), image, &error), "retail SizeOfImage 58E000 is accepted");
    check(image.size() == ExeImageSize, "retail image padded to the Steam size");
    check(all(image, 0x00596000u, 0x300u, 0), "import address table blanked");
    check(all(image, 0x00596300u, 0x1Cu, 0), "debug directory entry blanked");
    check(at(image, 0x00628380u) == 0, "metadata byte 628380 blanked");
    check(all(image, 0x006283BDu, 9u, 0), "metadata ahead of RSDS blanked");
    check(at(image, 0x0059631Cu) == Fill && at(image, 0x0062837Fu) == Fill && at(image, 0x00628381u) == Fill,
          "bytes around the debug metadata kept");
    check(at(image, 0x006283BCu) == Fill && at(image, 0x006283C6u) == Fill, "bytes around the RSDS lead-in kept");
    check(at(image, 0x00628304u) == Fill && at(image, 0x00628324u) == Fill, "game constants beside it kept");
}
void test_steam_layout() {
    std::vector<std::uint8_t> image; std::string error;
    check(exe_image_map_pe(pe(ExeImageSize), image, &error), "Steam SizeOfImage 590000 is accepted");
    check(all(image, 0x00596300u, 0x1Cu, 0), "debug metadata blanked in every layout");
}
void test_other_layouts() {
    std::vector<std::uint8_t> image; std::string error;
    check(!exe_image_map_pe(pe(0x0058F000u), image, &error), "another SizeOfImage is refused");
    check(!exe_image_map_pe(pe(ExeImageSize, 0x00800000u), image, &error), "another image base is refused");
}
}

int main() {
    try {
        test_retail_layout();
        test_steam_layout();
        test_other_layouts();
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "exe_image: " << checks << " checks passed\n";
    return 0;
}
