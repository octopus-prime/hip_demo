#include "app/options.h"
#include "saving/image.h"

#include <boost/program_options.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace hip_demo {
namespace {
void validate(const RenderOptions& options) {
    if (options.width <= 0 || options.height <= 0)
        throw std::runtime_error("--width and --height must be positive integers.");
    const auto row = (static_cast<std::uint64_t>(options.width) * 3 + 3) & ~3ull;
    if (row * options.height + 54 > std::numeric_limits<std::uint32_t>::max() ||
        static_cast<std::uint64_t>(options.width) * options.height > std::numeric_limits<int>::max())
        throw std::runtime_error("Image dimensions exceed BMP or renderer limits.");
    const auto& sampling = options.sampling;
    if (sampling.minimum <= 0 || sampling.specular_minimum <= 0 ||
        sampling.maximum <= 0 || sampling.batch <= 0)
        throw std::runtime_error("Sample counts and --batch-size must be positive integers.");
    if (!std::isfinite(sampling.relative_error) || sampling.relative_error <= 0 ||
        !std::isfinite(sampling.brightness_floor) || sampling.brightness_floor <= 0)
        throw std::runtime_error("--relative-error and --brightness-floor must be finite and positive.");
    if (options.device < -1)
        throw std::runtime_error("--device must be -1 (automatic) or a nonnegative HIP device index.");
    if (options.output.empty() || options.output.find('\0') != std::string::npos)
        throw std::runtime_error("--output must be a nonempty filename without NUL characters.");
    (void)image_format(options.output);
}
}

AppOptions load_options(int argc, char** argv) {
    namespace po = boost::program_options;
    AppOptions options;
    auto& render = options.render;
    auto& sampling = render.sampling;
    std::string mode;
    po::options_description rendering("Render options (also accepted in config files)");
    rendering.add_options()
        ("width", po::value<int>(&render.width)->default_value(render.width), "Image width in pixels")
        ("height", po::value<int>(&render.height)->default_value(render.height), "Image height in pixels")
        ("output,o", po::value<std::string>(&render.output)->default_value(render.output), "Output .png or .bmp filename")
        ("sampling", po::value<std::string>(&mode)->default_value("adaptive"), "Sampling mode: adaptive or fixed")
        ("samples", po::value<int>(&sampling.maximum)->default_value(sampling.maximum), "Maximum (fixed: exact) samples per pixel")
        ("min-samples", po::value<int>(&sampling.minimum)->default_value(sampling.minimum), "Adaptive minimum samples")
        ("specular-min-samples", po::value<int>(&sampling.specular_minimum)->default_value(sampling.specular_minimum), "Adaptive minimum for specular pixels")
        ("batch-size", po::value<int>(&sampling.batch)->default_value(sampling.batch), "Sample passes per GPU batch")
        ("relative-error", po::value<float>(&sampling.relative_error)->default_value(sampling.relative_error, "0.05"), "Adaptive relative noise threshold")
        ("brightness-floor", po::value<float>(&sampling.brightness_floor)->default_value(sampling.brightness_floor, "0.05"), "Adaptive brightness floor")
        ("device", po::value<int>(&render.device)->default_value(render.device), "HIP device index; -1 selects automatically");
    rendering.add_options()
        ("unified-traversal", po::value<bool>(&render.unified_traversal)->default_value(false)->implicit_value(true),
         "Experimental: traverse base custom geometry, meshes and instances in one HIP RT scene");
    po::options_description all("Usage: hiprt_demo [options]");
    all.add_options()
        ("help,h", "Show help without loading a scene or initializing the GPU")
        ("scene", po::value<std::string>(&options.scene)->default_value(options.scene), "Scene: classic, detail, or a .hipscene file")
        ("config", po::value<std::string>(&options.config_path), "Read render defaults from this config file")
        ("save-config", po::value<std::string>(&options.save_config_path), "Save effective render options and exit without rendering");
    all.add(rendering);
    po::variables_map values;
    const po::positional_options_description positional;
    const auto style = po::command_line_style::default_style & ~po::command_line_style::allow_guessing;
    po::store(po::command_line_parser(argc, argv).options(all).positional(positional).style(style).run(), values);
    if (values.count("help")) {
        options.help = true;
        std::ostringstream text;
        text << all << '\n';
        options.help_text = text.str();
        return options;
    }
    if (values.count("config")) {
        const auto path = values["config"].as<std::string>();
        if (path.empty()) throw std::runtime_error("--config requires a nonempty filename.");
        std::ifstream file(path);
        if (!file) throw std::runtime_error("Could not open render config: " + path);
        // Boost keeps explicit CLI values and replaces only defaulted values.
        po::store(po::parse_config_file(file, rendering), values);
        if (file.bad()) throw std::runtime_error("Failed while reading render config: " + path);
    }
    po::notify(values);
    if (options.scene.empty()) throw std::runtime_error("--scene requires a nonempty scene name or filename.");
    if (values.count("save-config") && options.save_config_path.empty())
        throw std::runtime_error("--save-config requires a nonempty filename.");
    if (mode != "adaptive" && mode != "fixed")
        throw std::runtime_error("--sampling must be adaptive or fixed.");
    sampling.adaptive = mode == "adaptive";
    validate(render);
    return options;
}

void save_render_config(const RenderOptions& options, const std::string& path) {
    validate(options);
    if (path.empty()) throw std::runtime_error("Render config filename must not be empty.");
    // Boost's INI reader strips # comments and surrounding whitespace, even in quotes.
    if (options.output.find_first_of("#\r\n") != std::string::npos ||
        options.output.find_first_not_of(" \t") != 0 ||
        options.output.find_last_not_of(" \t") != options.output.size() - 1)
        throw std::runtime_error("Cannot save --output in a Boost config: #, line breaks and surrounding whitespace are not supported.");
    const auto& sampling = options.sampling;
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(std::numeric_limits<float>::max_digits10)
         << "# Render defaults. Explicit command-line options override these values.\n"
         << "width = " << options.width << "\nheight = " << options.height
         << "\noutput = " << options.output
         << "\nsampling = " << (sampling.adaptive ? "adaptive" : "fixed")
         << "\nsamples = " << sampling.maximum << "\nmin-samples = " << sampling.minimum
         << "\nspecular-min-samples = " << sampling.specular_minimum << "\nbatch-size = " << sampling.batch
         << "\nrelative-error = " << sampling.relative_error << "\nbrightness-floor = " << sampling.brightness_floor
         << "\ndevice = " << options.device
         << "\nunified-traversal = " << (options.unified_traversal ? "true" : "false") << '\n';
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open render config for writing: " + path);
    file << text.str();
    file.close();
    if (!file) throw std::runtime_error("Failed while writing render config: " + path);
}

}
