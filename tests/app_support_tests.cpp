#include "app/options.h"
#include "saving/bmp.h"
#include "saving/png.h"

#include <boost/gil.hpp>
#include <boost/gil/extension/io/png.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

hip_demo::AppOptions options(std::vector<std::string> arguments) {
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    return hip_demo::load_options(static_cast<int>(argv.size()), argv.data());
}

void check_options(const std::string& fixture, std::filesystem::path temporary) {
    const auto original = options({"renderer", "--scene", fixture});
    const auto changed = options({"renderer", "--output", "changed.bmp", "--samples", "9",
        "--sampling", "fixed", "--width", "17", "--height", "9", "--scene", fixture});
    if (original.render.output != "render.png" || original.render.sampling.maximum != 1024 ||
        changed.render.output != "changed.bmp" || changed.render.sampling.maximum != 9 ||
        changed.render.sampling.adaptive || changed.render.width != 17 || changed.render.height != 9 ||
        changed.scene != original.scene) {
        throw std::runtime_error("CLI render options and scene selection are not independent");
    }
    temporary.replace_extension(".cfg");
    {
        std::ofstream file(temporary);
        file << "width = 320\nheight = 200\noutput = images with spaces.png\nsampling = fixed\n"
                "samples = 11\nmin-samples = 3\nspecular-min-samples = 7\nbatch-size = 4\n"
                "relative-error = 0.08\nbrightness-floor = 0.03\ndevice = 0\n";
    }
    const auto loaded = options({"renderer", "--config", temporary.string(), "--scene", fixture,
        "--width", "640", "--samples", "19", "--sampling", "adaptive"});
    const auto& render = loaded.render;
    if (render.width != 640 || render.height != 200 || render.output != "images with spaces.png" ||
        !render.sampling.adaptive || render.sampling.maximum != 19 || render.sampling.minimum != 3 ||
        render.sampling.specular_minimum != 7 || render.sampling.batch != 4 ||
        render.sampling.relative_error != 0.08f || render.sampling.brightness_floor != 0.03f || render.device != 0)
        throw std::runtime_error("CLI > config > defaults precedence failed");
    auto saved = render;
    saved.sampling.relative_error = 0.123456791f;
    hip_demo::save_render_config(saved, temporary.string());
    const auto restored = options({"renderer", "--config", temporary.string()}).render;
    if (restored.width != saved.width || restored.height != saved.height || restored.output != saved.output ||
        restored.sampling.relative_error != saved.sampling.relative_error ||
        restored.sampling.maximum != saved.sampling.maximum || restored.device != saved.device ||
        restored.sampling.minimum != saved.sampling.minimum ||
        restored.sampling.specular_minimum != saved.sampling.specular_minimum ||
        restored.sampling.batch != saved.sampling.batch ||
        restored.sampling.brightness_floor != saved.sampling.brightness_floor ||
        restored.sampling.adaptive != saved.sampling.adaptive)
        throw std::runtime_error("Saved effective render options did not round-trip");
    const auto help = options({"renderer", "--help", "--config", "nonexistent.cfg"});
    if (!help.help || help.help_text.find("--width") == std::string::npos ||
        help.help_text.find("--save-config") == std::string::npos)
        throw std::runtime_error("Help must describe options without reading files");
    const auto rejects = [](std::vector<std::string> arguments) {
        try { (void)options(arguments); }
        catch (const std::exception&) { return; }
        std::string message = "Invalid renderer options were accepted:";
        for (const auto& argument : arguments) message += " [" + argument + "]";
        throw std::runtime_error(message);
    };
    for (const auto& arguments : std::vector<std::vector<std::string>>{
        {"renderer", "old.bmp", "9"}, {"renderer", "--fixed"}, {"renderer", "--mesh", "x.ply"},
        {"renderer", "--wid", "17"}, {"renderer", "--width", "0"}, {"renderer", "--width", "17.5"},
        {"renderer", "--width", "2147483647"}, {"renderer", "--height", "-1"},
        {"renderer", "--samples", "0"}, {"renderer", "--batch-size", "0"},
        {"renderer", "--min-samples", "-1"}, {"renderer", "--specular-min-samples", "0"},
        {"renderer", "--relative-error", "nan"}, {"renderer", "--brightness-floor", "inf"},
        {"renderer", "--relative-error", "0"}, {"renderer", "--sampling", "unknown"},
        {"renderer", "--device", "-2"}, {"renderer", "--output", "bad.jpg"},
        {"renderer", "--config", "nonexistent.cfg"}, {"renderer", "--save-config", ""},
        {"renderer", "--scene", ""}, {"renderer", "--width", "1", "--width", "2"}})
        rejects(arguments);
    for (const auto* content : {"unknown = 1\n", "scene = detail\n", "width = 1\nwidth = 2\n",
                                "width = 0\n", "broken syntax\n"}) {
        { std::ofstream file(temporary); file << content; }
        rejects({"renderer", "--config", temporary.string()});
    }
    bool rejected_save = false;
    saved.output = "cannot#roundtrip.png";
    try { hip_demo::save_render_config(saved, temporary.string()); }
    catch (const std::runtime_error&) { rejected_save = true; }
    if (!rejected_save) throw std::runtime_error("Config serialization silently accepted a # comment in a path");
    bool rejected_write = false;
    try { hip_demo::save_render_config(render, (temporary / "cannot_create.cfg").string()); }
    catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("Could not open render config for writing:") == std::string::npos) throw;
        rejected_write = true;
    }
    if (!rejected_write) throw std::runtime_error("An unwritable config path was accepted");
    std::filesystem::remove(temporary);
}

void check_bmp(const std::filesystem::path& output) {
    const hip_demo::Image image{1, 2, {0x332211u, 0x665544u}};
    hip_demo::save_image(image, output.string());
    std::ifstream input(output, std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), {}};
    input.close();
    const std::array<unsigned char, 8> expected{0x33, 0x22, 0x11, 0, 0x66, 0x55, 0x44, 0};
    if (bytes.size() != 62 || bytes[0] != 'B' || bytes[1] != 'M' ||
        bytes[2] != bytes.size() || bytes[10] != 54 || bytes[18] != 1 ||
        bytes[22] != 0xfe || bytes[23] != 0xff || bytes[24] != 0xff || bytes[25] != 0xff ||
        !std::equal(expected.begin(), expected.end(), bytes.begin() + 54)) {
        throw std::runtime_error("BMP dimensions, top-down rows, BGR order, or padding are incorrect");
    }
    try {
        hip_demo::save_bmp(image, (output / "cannot_create.bmp").string());
        throw std::logic_error("An unwritable output path was accepted");
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("Could not open output image:") == std::string::npos) throw;
    }
    try {
        hip_demo::save_bmp({2, 2, {0}}, output.string());
        throw std::logic_error("An incomplete pixel buffer was accepted");
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("Invalid image dimensions or pixel count") == std::string::npos) throw;
    }
    std::filesystem::remove(output);
}

void check_png(std::filesystem::path output) {
    output.replace_extension(".PNG");
    const hip_demo::Image image{3, 2, {0x0000ffu, 0x00ff00u, 0xff0000u, 0x332211u, 0xffffffu, 0u}};
    hip_demo::save_image(image, output.string());
    boost::gil::rgb8_image_t decoded;
    boost::gil::read_image(output.string(), decoded, boost::gil::png_tag{});
    const auto pixels = boost::gil::const_view(decoded);
    if (pixels.width() != image.width || pixels.height() != image.height) {
        throw std::runtime_error("PNG dimensions changed");
    }
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto expected = image.pixels[x + static_cast<std::size_t>(y) * image.width];
            const auto pixel = pixels(x, y);
            if (boost::gil::at_c<0>(pixel) != (expected & 0xff) ||
                boost::gil::at_c<1>(pixel) != ((expected >> 8) & 0xff) ||
                boost::gil::at_c<2>(pixel) != ((expected >> 16) & 0xff)) {
                throw std::runtime_error("PNG RGB channels or top-down row order changed");
            }
        }
    }
    try {
        hip_demo::save_png(image, (output / "cannot_create.png").string());
        throw std::logic_error("An unwritable PNG path was accepted");
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("Could not open output image:") == std::string::npos) throw;
    }
    try {
        hip_demo::save_png({2, 2, {0}}, output.string());
        throw std::logic_error("An incomplete PNG pixel buffer was accepted");
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("Invalid image dimensions or pixel count") == std::string::npos) throw;
    }
    std::filesystem::remove(output);
    for (const auto* extension : {".jpg", ""}) {
        output.replace_extension(extension);
        try {
            hip_demo::save_image(image, output.string());
            throw std::logic_error("An unsupported output extension was accepted");
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()).find("use .bmp or .png") == std::string::npos) throw;
        }
        if (std::filesystem::exists(output)) throw std::runtime_error("Unsupported output created a file");
    }
}

}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Expected scene fixture and temporary BMP path");
        check_options(argv[1], argv[2]);
        check_bmp(argv[2]);
        check_png(argv[2]);
        std::cout << "CLI and CPU-only BMP/PNG boundary checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "App support checks failed: " << error.what() << '\n';
        return 1;
    }
}
