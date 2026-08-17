#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

// Yuri uses a very small OpenCV surface: BGRA/gray resize, box filtering, and
// affine/perspective warps.  Pulling the Android OpenCV stack onto Vita would
// be wasteful, so this compatibility layer implements precisely that CPU image
// contract.  It is not a UI or rendering frontend; Yuri still owns all layer
// composition and blending.

#define CV_8UC1 1
#define CV_8UC4 4

namespace cv {

constexpr int INTER_NEAREST = 0;
constexpr int INTER_LINEAR = 1;
constexpr int INTER_CUBIC = 2;
constexpr int INTER_AREA = 3;

struct Size {
    Size() = default;
    Size(int width_value, int height_value)
        : width(width_value), height(height_value) {}
    int width = 0;
    int height = 0;
};

struct Point2f {
    Point2f() = default;
    Point2f(float x_value, float y_value) : x(x_value), y(y_value) {}
    float x = 0.0f;
    float y = 0.0f;
};

class Mat {
public:
    Mat() = default;
    Mat(int row_count, int column_count, int image_type, void* pixels,
        std::size_t row_stride = 0)
        : rows(row_count), cols(column_count), type_(image_type), data_(pixels),
          step_(row_stride ? row_stride
                           : static_cast<std::size_t>(column_count) *
                                 channels_for(image_type)) {}

    int type() const { return type_; }
    std::size_t elemSize1() const {
        return has_transform_ ? sizeof(double) : 1u;
    }
    std::size_t step1(int = 0) const { return step_; }
    std::uint8_t* ptr(int row = 0) {
        if (!data_) return nullptr;
        return static_cast<std::uint8_t*>(data_) +
               static_cast<std::size_t>(row) * step_;
    }
    const std::uint8_t* ptr(int row = 0) const {
        if (!data_) return nullptr;
        return static_cast<const std::uint8_t*>(data_) +
               static_cast<std::size_t>(row) * step_;
    }
    template <typename T>
    T* ptr(int row = 0) {
        if (has_transform_)
            return reinterpret_cast<T*>(transform_.data()) + row * 3;
        return reinterpret_cast<T*>(ptr(row));
    }
    template <typename T>
    const T* ptr(int row = 0) const {
        if (has_transform_)
            return reinterpret_cast<const T*>(transform_.data()) + row * 3;
        return reinterpret_cast<const T*>(ptr(row));
    }

    void create(int row_count, int column_count, int image_type) {
        rows = row_count;
        cols = column_count;
        type_ = image_type;
        step_ = static_cast<std::size_t>(cols) * channels_for(type_);
        storage_ = std::make_shared<std::vector<std::uint8_t>>(
            step_ * static_cast<std::size_t>(std::max(0, rows)), 0);
        data_ = storage_->empty() ? nullptr : storage_->data();
    }

    int rows = 0;
    int cols = 0;

private:
    friend Mat make_transform(const std::array<double, 9>& values);
    friend const double* transform_values(const Mat& matrix);

    static std::size_t channels_for(int image_type) {
        return image_type == CV_8UC1 ? 1u : 4u;
    }

    int type_ = CV_8UC4;
    void* data_ = nullptr;
    std::size_t step_ = 0;
    std::shared_ptr<std::vector<std::uint8_t>> storage_;
    std::array<double, 9> transform_{};
    bool has_transform_ = false;
};

inline Mat make_transform(const std::array<double, 9>& values) {
    Mat result;
    result.transform_ = values;
    result.has_transform_ = true;
    return result;
}

inline const double* transform_values(const Mat& matrix) {
    return matrix.has_transform_ ? matrix.transform_.data() : nullptr;
}

inline int reflect101(int value, int limit) {
    if (limit <= 1) return 0;
    while (value < 0 || value >= limit) {
        if (value < 0) value = -value;
        if (value >= limit) value = limit * 2 - value - 2;
    }
    return value;
}

struct BoxFilterWorkspace {
    std::vector<std::uint32_t> column_sums;
    std::vector<std::uint8_t> delayed_rows;
};

inline void boxFilter(const Mat& source, Mat& destination, int, Size kernel,
                      BoxFilterWorkspace& workspace) {
    if (source.rows <= 0 || source.cols <= 0) return;
    if (!destination.ptr() || destination.rows != source.rows ||
        destination.cols != source.cols || destination.type() != source.type())
        destination.create(source.rows, source.cols, source.type());
    const int channels = source.type() == CV_8UC1 ? 1 : 4;
    const std::size_t row_bytes =
        static_cast<std::size_t>(source.cols) * channels;
    const auto byte_range = [row_bytes](const Mat& image) {
        const auto begin = reinterpret_cast<std::uintptr_t>(image.ptr());
        const auto end = begin +
            static_cast<std::size_t>(image.rows - 1) * image.step1() +
            row_bytes;
        return std::array<std::uintptr_t, 2>{{begin, end}};
    };
    const auto source_range = byte_range(source);
    const auto destination_range = byte_range(destination);
    const bool overlaps = source_range[0] < destination_range[1] &&
                          destination_range[0] < source_range[1];
    const bool exact_in_place = source.ptr() == destination.ptr() &&
                                source.step1() == destination.step1();
    Mat source_copy;
    const Mat* input_source = &source;
    if (overlaps && !exact_in_place) {
        source_copy.create(source.rows, source.cols, source.type());
        for (int y = 0; y < source.rows; ++y)
            std::memcpy(source_copy.ptr(y), source.ptr(y), row_bytes);
        input_source = &source_copy;
    }
    const int kernel_width = std::max(1, kernel.width);
    const int kernel_height = std::max(1, kernel.height);
    const int left = kernel_width / 2;
    const int top = kernel_height / 2;
    const unsigned divisor =
        static_cast<unsigned>(kernel_width * kernel_height);
    constexpr unsigned box_divider_shift = 23;
    unsigned box_divider_scale = 0;
    unsigned box_divider_delta = 0;
    if (divisor > 1 && divisor <= 256) {
        double scaled = static_cast<double>(1u << box_divider_shift) /
                        divisor;
        box_divider_scale = static_cast<unsigned>(std::floor(scaled));
        scaled -= box_divider_scale;
        box_divider_delta = divisor / 2;
        if (scaled < 0.5)
            ++box_divider_delta;
        else
            ++box_divider_scale;
    }
    constexpr int neon_u16_lanes = 8;
    const int box_float_elements =
        (destination.cols * channels / neon_u16_lanes) * neon_u16_lanes;
    const float box_float_scale = static_cast<float>(1.0 / divisor);
    const int delayed_row_count = exact_in_place
        ? std::min(top + 1, source.rows)
        : 0;
    if (exact_in_place)
        workspace.delayed_rows.resize(
            static_cast<std::size_t>(delayed_row_count) * row_bytes);
    workspace.column_sums.assign(
        static_cast<std::size_t>(source.cols) * channels, 0);
    for (int ky = 0; ky < kernel_height; ++ky) {
        const auto* input = input_source->ptr(
            reflect101(ky - top, source.rows));
        for (int x = 0; x < source.cols; ++x)
            for (int channel = 0; channel < channels; ++channel)
                workspace.column_sums[x * channels + channel] +=
                    input[x * channels + channel];
    }

    for (int y = 0; y < destination.rows; ++y) {
        auto* output = exact_in_place
            ? workspace.delayed_rows.data() +
                  static_cast<std::size_t>(y % delayed_row_count) * row_bytes
            : destination.ptr(y);
        std::uint32_t sums[4]{};
        for (int kx = 0; kx < kernel_width; ++kx) {
            const int sx = reflect101(kx - left, source.cols);
            for (int channel = 0; channel < channels; ++channel)
                sums[channel] +=
                    workspace.column_sums[sx * channels + channel];
        }
        for (int x = 0; x < destination.cols; ++x) {
            for (int channel = 0; channel < channels; ++channel) {
                int value;
                if (divisor == 1) {
                    value = static_cast<int>(sums[channel]);
                } else if (divisor <= 256) {
                    value = static_cast<int>(
                        (static_cast<std::uint64_t>(
                             sums[channel] + box_divider_delta) *
                         box_divider_scale) >> box_divider_shift);
                } else {
                    const int element = x * channels + channel;
                    value = element < box_float_elements
                        ? static_cast<int>(std::lrintf(
                              static_cast<float>(sums[channel]) *
                              box_float_scale))
                        : static_cast<int>(std::lrint(
                              sums[channel] * (1.0 / divisor)));
                }
                output[x * channels + channel] =
                    static_cast<std::uint8_t>(std::clamp(value, 0, 255));
            }
            if (x + 1 == destination.cols) break;

            const int remove_x = reflect101(x - left, source.cols);
            const int add_x =
                reflect101(x + kernel_width - left, source.cols);
            for (int channel = 0; channel < channels; ++channel) {
                sums[channel] -= workspace.column_sums[
                    remove_x * channels + channel];
                sums[channel] += workspace.column_sums[
                    add_x * channels + channel];
            }
        }

        if (y + 1 < destination.rows) {
            const auto* remove_row = input_source->ptr(
                reflect101(y - top, source.rows));
            const auto* add_row = input_source->ptr(
                reflect101(y + kernel_height - top, source.rows));
            for (int x = 0; x < source.cols; ++x)
                for (int channel = 0; channel < channels; ++channel) {
                    workspace.column_sums[x * channels + channel] -=
                        remove_row[x * channels + channel];
                    workspace.column_sums[x * channels + channel] +=
                        add_row[x * channels + channel];
                }
        }

        // The row that just left the vertical window will not be read again.
        // Commit its already-filtered replacement only after updating the
        // rolling sums, keeping exact in-place filters alias-safe with a
        // bounded top+1-row delay instead of cloning the full image.
        if (exact_in_place && y >= top) {
            const int flush_y = y - top;
            std::memcpy(destination.ptr(flush_y),
                        workspace.delayed_rows.data() +
                            static_cast<std::size_t>(
                                flush_y % delayed_row_count) * row_bytes,
                        row_bytes);
        }
    }
    if (exact_in_place) {
        const int first_unflushed = std::max(0, destination.rows - top);
        for (int y = first_unflushed; y < destination.rows; ++y)
            std::memcpy(destination.ptr(y),
                        workspace.delayed_rows.data() +
                            static_cast<std::size_t>(
                                y % delayed_row_count) * row_bytes,
                        row_bytes);
    }
}

inline void boxFilter(const Mat& source, Mat& destination, int depth,
                      Size kernel) {
    BoxFilterWorkspace workspace;
    boxFilter(source, destination, depth, kernel, workspace);
}

inline int round_to_int(float value) {
    if (value <= static_cast<float>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    if (value >= static_cast<float>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(std::lrintf(value));
}

inline int round_to_int(double value) {
    if (value <= static_cast<double>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    if (value >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(std::lrint(value));
}

inline void sample(const Mat& source, double x, double y, int interpolation,
                   std::uint8_t* output) {
    const int channels = source.type() == CV_8UC1 ? 1 : 4;
    if (interpolation == INTER_NEAREST) {
        const int sx = static_cast<int>(std::floor(x + 0.5));
        const int sy = static_cast<int>(std::floor(y + 0.5));
        if (sx < 0 || sx >= source.cols || sy < 0 || sy >= source.rows) {
            std::memset(output, 0, channels);
            return;
        }
        std::memcpy(output, source.ptr(sy) + sx * channels, channels);
        return;
    }

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    for (int channel = 0; channel < channels; ++channel) {
        double value = 0.0;
        for (int iy = 0; iy < 2; ++iy) {
            const int sy = y0 + iy;
            if (sy < 0 || sy >= source.rows) continue;
            const double wy = iy ? fy : 1.0 - fy;
            for (int ix = 0; ix < 2; ++ix) {
                const int sx = x0 + ix;
                if (sx < 0 || sx >= source.cols) continue;
                const double wx = ix ? fx : 1.0 - fx;
                value += source.ptr(sy)[sx * channels + channel] * wx * wy;
            }
        }
        output[channel] = static_cast<std::uint8_t>(
            std::clamp(std::lround(value), 0l, 255l));
    }
}

struct ResizeWorkspace {
    struct AreaContribution {
        int source = 0;
        float weight = 0.0f;
    };

    std::vector<int> x_offsets;
    std::vector<std::int16_t> x_coefficients;
    std::vector<int> y_offsets;
    std::vector<std::int16_t> y_coefficients;
    std::vector<int> area_x_offsets;
    std::vector<AreaContribution> area_x;
    std::vector<int> area_y_offsets;
    std::vector<AreaContribution> area_y;
    int source_width = 0;
    int source_height = 0;
    int destination_width = 0;
    int destination_height = 0;
    int interpolation = INTER_NEAREST;
    double scale_x = 0.0;
    double scale_y = 0.0;
    double inverse_scale_x = 0.0;
    double inverse_scale_y = 0.0;
    bool area_downscale = false;
};

struct ResizeTaskWorkspace {
    std::vector<float> area_rows;
    std::vector<float> area_output;
    std::array<int, 4> area_source_rows{{-1, -1, -1, -1}};
    int area_next_slot = 0;
    std::vector<int> cubic_rows;
    std::array<int, 4> cubic_source_rows{{-1, -1, -1, -1}};
};

inline void prepareResizeTaskWorkspace(const Mat& source, Size size,
                                       int interpolation,
                                       ResizeTaskWorkspace& workspace) {
    const int channels = source.type() == CV_8UC1 ? 1 : 4;
    const std::size_t row_elements =
        static_cast<std::size_t>(size.width) * channels;
    if (interpolation == INTER_AREA) {
        workspace.area_rows.resize(row_elements * 4);
        workspace.area_output.resize(row_elements);
    } else if (interpolation == INTER_CUBIC) {
        workspace.cubic_rows.resize(row_elements * 4);
    }
}

inline void append_area_contributions(
    int source_size, int destination_size,
    std::vector<int>& offsets,
    std::vector<ResizeWorkspace::AreaContribution>& contributions) {
    offsets.resize(static_cast<std::size_t>(destination_size) + 1);
    contributions.clear();
    contributions.reserve(static_cast<std::size_t>(source_size) * 2);
    const double scale =
        static_cast<double>(source_size) / destination_size;
    for (int destination = 0; destination < destination_size;
         ++destination) {
        offsets[destination] = static_cast<int>(contributions.size());
        const double first = destination * scale;
        const double last = first + scale;
        const double cell_width = std::min(scale, source_size - first);
        int source_first = static_cast<int>(std::ceil(first));
        int source_last = static_cast<int>(std::floor(last));
        source_last = std::min(source_last, source_size - 1);
        source_first = std::min(source_first, source_last);
        if (source_first - first > 1e-3)
            contributions.push_back({
                source_first - 1,
                static_cast<float>((source_first - first) / cell_width)});
        for (int source = source_first; source < source_last; ++source)
            contributions.push_back(
                {source, static_cast<float>(1.0 / cell_width)});
        if (last - source_last > 1e-3)
            contributions.push_back({
                source_last,
                static_cast<float>(
                    std::min(std::min(last - source_last, 1.0), cell_width) /
                    cell_width)});
    }
    offsets[destination_size] = static_cast<int>(contributions.size());
}

inline std::int16_t resize_coefficient(float value) {
    return static_cast<std::int16_t>(std::clamp(round_to_int(value * 2048.0f),
                                                -32768, 32767));
}

inline void cubic_coefficients_float(float fraction, float output[4]) {
    constexpr float coefficient = -0.75f;
    output[0] = ((coefficient * (fraction + 1.0f) - 5.0f * coefficient) *
                     (fraction + 1.0f) +
                 8.0f * coefficient) *
                    (fraction + 1.0f) -
                4.0f * coefficient;
    output[1] = ((coefficient + 2.0f) * fraction -
                 (coefficient + 3.0f)) * fraction * fraction + 1.0f;
    const float inverse = 1.0f - fraction;
    output[2] = ((coefficient + 2.0f) * inverse -
                 (coefficient + 3.0f)) * inverse * inverse + 1.0f;
    output[3] = 1.0f - output[0] - output[1] - output[2];
}

inline void cubic_coefficients(float fraction, std::int16_t output[4]) {
    float values[4];
    cubic_coefficients_float(fraction, values);
    for (int index = 0; index < 4; ++index)
        output[index] = resize_coefficient(values[index]);
}

inline void prepareResizeWorkspace(const Mat& source, Size size,
                                   int interpolation,
                                   ResizeWorkspace& workspace) {
    if (workspace.source_width == source.cols &&
        workspace.source_height == source.rows &&
        workspace.destination_width == size.width &&
        workspace.destination_height == size.height &&
        workspace.interpolation == interpolation)
        return;
    workspace.source_width = source.cols;
    workspace.source_height = source.rows;
    workspace.destination_width = size.width;
    workspace.destination_height = size.height;
    workspace.interpolation = interpolation;
    workspace.area_downscale = false;
    workspace.x_offsets.resize(static_cast<std::size_t>(size.width));

    workspace.inverse_scale_x =
        static_cast<double>(size.width) / source.cols;
    workspace.inverse_scale_y =
        static_cast<double>(size.height) / source.rows;
    workspace.scale_x = 1.0 / workspace.inverse_scale_x;
    workspace.scale_y = 1.0 / workspace.inverse_scale_y;
    const double scale_x = workspace.scale_x;
    if (interpolation == INTER_NEAREST) {
        workspace.x_coefficients.clear();
        workspace.y_coefficients.clear();
        workspace.y_offsets.resize(static_cast<std::size_t>(size.height));
        for (int x = 0; x < size.width; ++x) {
            const int source_x = static_cast<int>(std::floor(x * scale_x));
            workspace.x_offsets[x] = std::min(source_x, source.cols - 1);
        }
        const double scale_y = workspace.scale_y;
        for (int y = 0; y < size.height; ++y) {
            const int source_y = static_cast<int>(std::floor(y * scale_y));
            workspace.y_offsets[y] = std::min(source_y, source.rows - 1);
        }
        return;
    }

    const double scale_y = workspace.scale_y;
    if (interpolation == INTER_AREA && scale_x >= 1.0 && scale_y >= 1.0) {
        workspace.x_coefficients.clear();
        workspace.y_offsets.clear();
        workspace.y_coefficients.clear();
        workspace.area_downscale = true;
        append_area_contributions(
            source.cols, size.width, workspace.area_x_offsets,
            workspace.area_x);
        append_area_contributions(
            source.rows, size.height, workspace.area_y_offsets,
            workspace.area_y);
        return;
    }

    workspace.area_x_offsets.clear();
    workspace.area_x.clear();
    workspace.area_y_offsets.clear();
    workspace.area_y.clear();

    if (interpolation == INTER_LINEAR || interpolation == INTER_AREA) {
        workspace.x_coefficients.resize(
            static_cast<std::size_t>(size.width) * 2);
        for (int x = 0; x < size.width; ++x) {
            float fraction;
            int source_x;
            if (interpolation == INTER_AREA) {
                source_x = static_cast<int>(std::floor(x * scale_x));
                fraction = static_cast<float>(
                    (x + 1) -
                    (source_x + 1) * workspace.inverse_scale_x);
                fraction = fraction <= 0.0f
                               ? 0.0f
                               : fraction - std::floor(fraction);
            } else {
                fraction = static_cast<float>(
                    (x + 0.5) * scale_x - 0.5);
                source_x = static_cast<int>(std::floor(fraction));
                fraction -= source_x;
            }
            if (source_x < 0) {
                source_x = 0;
                fraction = 0.0f;
            }
            if (source_x >= source.cols - 1) {
                source_x = source.cols - 1;
                fraction = 0.0f;
            }
            workspace.x_offsets[x] = source_x;
            workspace.x_coefficients[x * 2] =
                resize_coefficient(1.0f - fraction);
            workspace.x_coefficients[x * 2 + 1] =
                resize_coefficient(fraction);
        }
        workspace.y_offsets.resize(static_cast<std::size_t>(size.height));
        workspace.y_coefficients.resize(
            static_cast<std::size_t>(size.height) * 2);
        for (int y = 0; y < size.height; ++y) {
            float fraction;
            int source_y;
            if (interpolation == INTER_AREA) {
                source_y = static_cast<int>(std::floor(y * scale_y));
                fraction = static_cast<float>(
                    (y + 1) -
                    (source_y + 1) * workspace.inverse_scale_y);
                fraction = fraction <= 0.0f
                               ? 0.0f
                               : fraction - std::floor(fraction);
            } else {
                fraction = static_cast<float>(
                    (y + 0.5) * scale_y - 0.5);
                source_y = static_cast<int>(std::floor(fraction));
                fraction -= source_y;
            }
            workspace.y_offsets[y] = source_y;
            workspace.y_coefficients[y * 2] =
                resize_coefficient(1.0f - fraction);
            workspace.y_coefficients[y * 2 + 1] =
                resize_coefficient(fraction);
        }
        return;
    }

    if (interpolation == INTER_CUBIC) {
        workspace.x_coefficients.resize(
            static_cast<std::size_t>(size.width) * 4);
        for (int x = 0; x < size.width; ++x) {
            float fraction = static_cast<float>(
                (x + 0.5) * scale_x - 0.5);
            const int source_x = static_cast<int>(std::floor(fraction));
            fraction -= source_x;
            workspace.x_offsets[x] = source_x;
            cubic_coefficients(
                fraction, workspace.x_coefficients.data() + x * 4);
        }
        workspace.y_offsets.resize(static_cast<std::size_t>(size.height));
        workspace.y_coefficients.resize(
            static_cast<std::size_t>(size.height) * 4);
        for (int y = 0; y < size.height; ++y) {
            float fraction = static_cast<float>(
                (y + 0.5) * scale_y - 0.5);
            const int source_y = static_cast<int>(std::floor(fraction));
            fraction -= source_y;
            workspace.y_offsets[y] = source_y;
            cubic_coefficients(
                fraction, workspace.y_coefficients.data() + y * 4);
        }
    }
}

template <typename Callback>
inline void for_area_contributions(int source_size, int destination_index,
                                   double scale, Callback&& callback) {
    const double first = destination_index * scale;
    const double last = first + scale;
    const double cell_width = std::min(scale, source_size - first);
    int source_first = static_cast<int>(std::ceil(first));
    int source_last = static_cast<int>(std::floor(last));
    source_last = std::min(source_last, source_size - 1);
    source_first = std::min(source_first, source_last);
    if (source_first - first > 1e-3)
        callback(source_first - 1,
                 static_cast<float>((source_first - first) / cell_width));
    for (int source = source_first; source < source_last; ++source)
        callback(source, static_cast<float>(1.0 / cell_width));
    if (last - source_last > 1e-3)
        callback(source_last, static_cast<float>(
            std::min(std::min(last - source_last, 1.0), cell_width) /
            cell_width));
}

inline void resizeRows(const Mat& source, Mat& destination, Size size,
                       int interpolation, const ResizeWorkspace& workspace,
                       ResizeTaskWorkspace& task_workspace,
                       int row_begin, int row_end) {
    const int channels = source.type() == CV_8UC1 ? 1 : 4;
    const double scale_y = workspace.scale_y;
    row_begin = std::clamp(row_begin, 0, size.height);
    row_end = std::clamp(row_end, row_begin, size.height);

    if (interpolation == INTER_NEAREST) {
        if (source.type() == CV_8UC4) {
            for (int y = row_begin; y < row_end; ++y) {
                const int source_y = workspace.y_offsets[y];
                const auto* input = source.ptr(source_y);
                auto* output = destination.ptr(y);
                for (int x = 0; x < size.width; ++x) {
                    std::uint32_t pixel;
                    std::memcpy(&pixel,
                                input + workspace.x_offsets[x] * 4,
                                sizeof(pixel));
                    std::memcpy(output + x * 4, &pixel, sizeof(pixel));
                }
            }
            return;
        }
        if (source.type() == CV_8UC1) {
            for (int y = row_begin; y < row_end; ++y) {
                const int source_y = workspace.y_offsets[y];
                const auto* input = source.ptr(source_y);
                auto* output = destination.ptr(y);
                for (int x = 0; x < size.width; ++x)
                    output[x] = input[workspace.x_offsets[x]];
            }
            return;
        }
        for (int y = row_begin; y < row_end; ++y) {
            const int source_y = workspace.y_offsets[y];
            const auto* input = source.ptr(source_y);
            auto* output = destination.ptr(y);
            for (int x = 0; x < size.width; ++x) {
                const int source_x = workspace.x_offsets[x];
                std::memcpy(output + x * channels,
                            input + source_x * channels, channels);
            }
        }
        return;
    }

    if (interpolation == INTER_AREA && workspace.area_downscale) {
        const double scale_x = workspace.scale_x;
        const bool integer_scale =
            std::abs(scale_x - std::round(scale_x)) <
                std::numeric_limits<double>::epsilon() &&
            std::abs(scale_y - std::round(scale_y)) <
                std::numeric_limits<double>::epsilon();
        const int integer_x = static_cast<int>(std::round(scale_x));
        const int integer_y = static_cast<int>(std::round(scale_y));
        if (integer_scale) {
            for (int y = row_begin; y < row_end; ++y) {
                auto* output = destination.ptr(y);
                for (int x = 0; x < size.width; ++x) {
                    for (int channel = 0; channel < channels; ++channel) {
                        std::uint32_t sum = 0;
                        for (int source_y = y * integer_y;
                             source_y < std::min((y + 1) * integer_y,
                                                 source.rows);
                             ++source_y) {
                            const auto* input = source.ptr(source_y);
                            for (int source_x = x * integer_x;
                                 source_x < std::min((x + 1) * integer_x,
                                                     source.cols);
                                 ++source_x)
                                sum += input[source_x * channels + channel];
                        }
                        const int area = integer_x * integer_y;
                        const int value = integer_x == 2 && integer_y == 2
                                              ? static_cast<int>((sum + 2) >> 2)
                                              : round_to_int(
                                                    static_cast<float>(sum) *
                                                    (1.0f / area));
                        output[x * channels + channel] =
                            static_cast<std::uint8_t>(
                                std::clamp(value, 0, 255));
                    }
                }
            }
            return;
        }

        const std::size_t row_elements =
            static_cast<std::size_t>(size.width) * channels;
        // The owner thread sizes this scratch before dispatch. Workers only
        // mutate their private lane and never allocate while a draw is live.
        if (task_workspace.area_rows.size() < row_elements * 4 ||
            task_workspace.area_output.size() < row_elements)
            return;
        task_workspace.area_source_rows = {{-1, -1, -1, -1}};
        task_workspace.area_next_slot = 0;
        for (int y = row_begin; y < row_end; ++y) {
            std::fill(task_workspace.area_output.begin(),
                      task_workspace.area_output.end(), 0.0f);
            const int y_begin = workspace.area_y_offsets[y];
            const int y_end = workspace.area_y_offsets[y + 1];
            for (int yi = y_begin; yi < y_end; ++yi) {
                const auto& y_contribution = workspace.area_y[yi];
                int slot = -1;
                for (int candidate = 0; candidate < 4; ++candidate)
                    if (task_workspace.area_source_rows[candidate] ==
                        y_contribution.source) {
                        slot = candidate;
                        break;
                    }
                if (slot < 0) {
                    slot = task_workspace.area_next_slot;
                    task_workspace.area_next_slot =
                        (task_workspace.area_next_slot + 1) % 4;
                    task_workspace.area_source_rows[slot] =
                        y_contribution.source;
                    const auto* input = source.ptr(y_contribution.source);
                    float* horizontal = task_workspace.area_rows.data() +
                        static_cast<std::size_t>(slot) * row_elements;
                    for (int x = 0; x < size.width; ++x) {
                        const int x_begin = workspace.area_x_offsets[x];
                        const int x_end = workspace.area_x_offsets[x + 1];
                        for (int channel = 0; channel < channels; ++channel) {
                            float sum = 0.0f;
                            for (int xi = x_begin; xi < x_end; ++xi) {
                                const auto& contribution =
                                    workspace.area_x[xi];
                                sum += input[
                                    contribution.source * channels +
                                    channel] * contribution.weight;
                            }
                            horizontal[x * channels + channel] = sum;
                        }
                    }
                }
                const float* horizontal = task_workspace.area_rows.data() +
                    static_cast<std::size_t>(slot) * row_elements;
                for (std::size_t element = 0; element < row_elements;
                     ++element)
                    task_workspace.area_output[element] +=
                        y_contribution.weight * horizontal[element];
            }
            auto* output = destination.ptr(y);
            for (std::size_t element = 0; element < row_elements; ++element)
                output[element] = static_cast<std::uint8_t>(std::clamp(
                    round_to_int(task_workspace.area_output[element]),
                    0, 255));
        }
        return;
    }

    if (interpolation == INTER_LINEAR || interpolation == INTER_AREA) {
        for (int y = row_begin; y < row_end; ++y) {
            const int source_y = workspace.y_offsets[y];
            const std::int16_t beta0 = workspace.y_coefficients[y * 2];
            const std::int16_t beta1 =
                workspace.y_coefficients[y * 2 + 1];
            const int source_y0 =
                std::clamp(source_y, 0, source.rows - 1);
            const int source_y1 =
                std::clamp(source_y + 1, 0, source.rows - 1);
            const auto* input0 = source.ptr(source_y0);
            const auto* input1 = source.ptr(source_y1);
            auto* output = destination.ptr(y);
            for (int x = 0; x < size.width; ++x) {
                const int source_x = workspace.x_offsets[x];
                const int source_x1 = std::min(source_x + 1, source.cols - 1);
                const std::int16_t alpha0 =
                    workspace.x_coefficients[x * 2];
                const std::int16_t alpha1 =
                    workspace.x_coefficients[x * 2 + 1];
                for (int channel = 0; channel < channels; ++channel) {
                    const int horizontal0 =
                        input0[source_x * channels + channel] * alpha0 +
                        input0[source_x1 * channels + channel] * alpha1;
                    const int horizontal1 =
                        input1[source_x * channels + channel] * alpha0 +
                        input1[source_x1 * channels + channel] * alpha1;
                    const int value =
                        (((beta0 * (horizontal0 >> 4)) >> 16) +
                         ((beta1 * (horizontal1 >> 4)) >> 16) + 2) >> 2;
                    output[x * channels + channel] =
                        static_cast<std::uint8_t>(std::clamp(value, 0, 255));
                }
            }
        }
        return;
    }

    if (interpolation == INTER_CUBIC) {
        const std::size_t row_elements =
            static_cast<std::size_t>(size.width) * channels;
        if (task_workspace.cubic_rows.size() < row_elements * 4) return;
        task_workspace.cubic_source_rows = {{-1, -1, -1, -1}};
        for (int y = row_begin; y < row_end; ++y) {
            const int source_y = workspace.y_offsets[y];
            const std::int16_t* beta =
                workspace.y_coefficients.data() + y * 4;
            int row_slots[4];
            for (int tap_y = 0; tap_y < 4; ++tap_y) {
                const int clamped_y = std::clamp(
                    source_y - 1 + tap_y, 0, source.rows - 1);
                int slot = -1;
                for (int candidate = 0; candidate < 4; ++candidate)
                    if (task_workspace.cubic_source_rows[candidate] ==
                        clamped_y) {
                        slot = candidate;
                        break;
                    }
                if (slot < 0) {
                    for (int candidate = 0; candidate < 4; ++candidate) {
                        bool needed = false;
                        for (int needed_tap = 0; needed_tap < 4;
                             ++needed_tap) {
                            const int needed_y = std::clamp(
                                source_y - 1 + needed_tap, 0,
                                source.rows - 1);
                            if (task_workspace.cubic_source_rows[candidate] ==
                                needed_y) {
                                needed = true;
                                break;
                            }
                        }
                        if (!needed) {
                            slot = candidate;
                            break;
                        }
                    }
                    if (slot < 0) slot = tap_y;
                    task_workspace.cubic_source_rows[slot] = clamped_y;
                    const auto* input = source.ptr(clamped_y);
                    int* horizontal = task_workspace.cubic_rows.data() +
                        static_cast<std::size_t>(slot) * row_elements;
                    for (int x = 0; x < size.width; ++x) {
                        const int source_x = workspace.x_offsets[x];
                        const auto* alpha =
                            workspace.x_coefficients.data() + x * 4;
                        for (int channel = 0; channel < channels; ++channel) {
                            int value = 0;
                            for (int tap_x = 0; tap_x < 4; ++tap_x) {
                                const int clamped_x = std::clamp(
                                    source_x - 1 + tap_x, 0,
                                    source.cols - 1);
                                value += input[
                                    clamped_x * channels + channel] *
                                    alpha[tap_x];
                            }
                            horizontal[x * channels + channel] = value;
                        }
                    }
                }
                row_slots[tap_y] = slot;
            }
            auto* output = destination.ptr(y);
            for (int x = 0; x < size.width; ++x) {
                for (int channel = 0; channel < channels; ++channel) {
                    int horizontal[4];
                    for (int tap_y = 0; tap_y < 4; ++tap_y)
                        horizontal[tap_y] = task_workspace.cubic_rows[
                            static_cast<std::size_t>(row_slots[tap_y]) *
                                row_elements +
                            x * channels + channel];
                    const int value =
                        (horizontal[0] * beta[0] +
                         horizontal[1] * beta[1] +
                         horizontal[2] * beta[2] +
                         horizontal[3] * beta[3] + (1 << 21)) >> 22;
                    output[x * channels + channel] =
                        static_cast<std::uint8_t>(std::clamp(value, 0, 255));
                }
            }
        }
        return;
    }

    for (int y = row_begin; y < row_end; ++y) {
        auto* output = destination.ptr(y);
        const double source_y = (y + 0.5) * scale_y - 0.5;
        for (int x = 0; x < size.width; ++x) {
            const double source_x =
                (x + 0.5) * static_cast<double>(source.cols) / size.width -
                0.5;
            sample(source, source_x, source_y, interpolation,
                   output + x * channels);
        }
    }
}

inline void resizeRows(const Mat& source, Mat& destination, Size size,
                       int interpolation, const ResizeWorkspace& workspace,
                       int row_begin, int row_end) {
    ResizeTaskWorkspace task_workspace;
    prepareResizeTaskWorkspace(source, size, interpolation, task_workspace);
    resizeRows(source, destination, size, interpolation, workspace,
               task_workspace, row_begin, row_end);
}

inline void resize(const Mat& source, Mat& destination, Size size, double,
                   double, int interpolation) {
    if (size.width <= 0 || size.height <= 0 || source.cols <= 0 ||
        source.rows <= 0)
        return;
    const bool aliases_source = destination.ptr() == source.ptr() &&
                                destination.ptr() != nullptr;
    if (aliases_source && source.cols == size.width &&
        source.rows == size.height && destination.cols == size.width &&
        destination.rows == size.height &&
        destination.type() == source.type())
        return;
    Mat preserved_source;
    const Mat* input_source = &source;
    if (aliases_source) {
        preserved_source.create(source.rows, source.cols, source.type());
        const std::size_t source_row_bytes =
            static_cast<std::size_t>(source.cols) *
            (source.type() == CV_8UC1 ? 1 : 4);
        for (int row = 0; row < source.rows; ++row)
            std::memcpy(preserved_source.ptr(row), source.ptr(row),
                        source_row_bytes);
        input_source = &preserved_source;
    }
    if (!destination.ptr() || destination.cols != size.width ||
        destination.rows != size.height || destination.type() != source.type())
        destination.create(size.height, size.width, source.type());
    ResizeWorkspace workspace;
    ResizeTaskWorkspace task_workspace;
    prepareResizeWorkspace(*input_source, size, interpolation, workspace);
    prepareResizeTaskWorkspace(
        *input_source, size, interpolation, task_workspace);
    resizeRows(*input_source, destination, size, interpolation, workspace,
               task_workspace, 0, size.height);
}

template <std::size_t MatrixSize, std::size_t RightHandSides>
inline bool solve_lu(
    double (&matrix)[MatrixSize][MatrixSize],
    double (&right_hand_side)[MatrixSize][RightHandSides]) {
    constexpr double epsilon =
        std::numeric_limits<double>::epsilon() * 100.0;
    for (std::size_t column = 0; column < MatrixSize; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < MatrixSize; ++row)
            if (std::abs(matrix[row][column]) >
                std::abs(matrix[pivot][column]))
                pivot = row;
        if (std::abs(matrix[pivot][column]) < epsilon) return false;
        if (pivot != column) {
            for (std::size_t value = column; value < MatrixSize; ++value)
                std::swap(matrix[column][value], matrix[pivot][value]);
            for (std::size_t value = 0; value < RightHandSides; ++value)
                std::swap(right_hand_side[column][value],
                          right_hand_side[pivot][value]);
        }

        const double inverse_pivot = -1.0 / matrix[column][column];
        for (std::size_t row = column + 1; row < MatrixSize; ++row) {
            const double alpha = matrix[row][column] * inverse_pivot;
            for (std::size_t value = column + 1; value < MatrixSize;
                 ++value)
                matrix[row][value] +=
                    alpha * matrix[column][value];
            for (std::size_t value = 0; value < RightHandSides; ++value)
                right_hand_side[row][value] +=
                    alpha * right_hand_side[column][value];
        }
    }

    for (std::size_t reverse_row = MatrixSize; reverse_row-- > 0;) {
        for (std::size_t value = 0; value < RightHandSides; ++value) {
            double sum = right_hand_side[reverse_row][value];
            for (std::size_t column = reverse_row + 1;
                 column < MatrixSize; ++column)
                sum -= matrix[reverse_row][column] *
                       right_hand_side[column][value];
            right_hand_side[reverse_row][value] =
                sum / matrix[reverse_row][reverse_row];
        }
    }
    return true;
}

inline Mat getPerspectiveTransform(const Point2f source[4],
                                   const Point2f destination[4]) {
    double system[8][8]{};
    double result[8][1]{};
    for (int point = 0; point < 4; ++point) {
        system[point][0] = system[point + 4][3] = source[point].x;
        system[point][1] = system[point + 4][4] = source[point].y;
        system[point][2] = system[point + 4][5] = 1.0;
        system[point][6] = -source[point].x * destination[point].x;
        system[point][7] = -source[point].y * destination[point].x;
        system[point + 4][6] =
            -source[point].x * destination[point].y;
        system[point + 4][7] =
            -source[point].y * destination[point].y;
        result[point][0] = destination[point].x;
        result[point + 4][0] = destination[point].y;
    }
    if (!solve_lu(system, result))
        return make_transform({0, 0, 0, 0, 0, 0, 0, 0, 1});
    return make_transform({result[0][0], result[1][0], result[2][0],
                           result[3][0], result[4][0], result[5][0],
                           result[6][0], result[7][0], 1.0});
}

inline Mat getAffineTransform(const Point2f source[3],
                              const Point2f destination[3]) {
    double system[6][6]{};
    double result[6][1]{};
    for (int point = 0; point < 3; ++point) {
        system[point * 2][0] = source[point].x;
        system[point * 2][1] = source[point].y;
        system[point * 2][2] = 1.0;
        system[point * 2 + 1][3] = source[point].x;
        system[point * 2 + 1][4] = source[point].y;
        system[point * 2 + 1][5] = 1.0;
        result[point * 2][0] = destination[point].x;
        result[point * 2 + 1][0] = destination[point].y;
    }
    if (!solve_lu(system, result))
        return make_transform({0, 0, 0, 0, 0, 0, 0, 0, 1});
    return make_transform({result[0][0], result[1][0], result[2][0],
                           result[3][0], result[4][0], result[5][0],
                           0.0, 0.0, 1.0});
}

inline bool invert_perspective(const double* input, double output[9]) {
    double matrix[3][3];
    double inverse[3][3]{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            matrix[row][column] = input[row * 3 + column];
        inverse[row][row] = 1.0;
    }
    if (!solve_lu(matrix, inverse)) {
        std::fill(output, output + 9, 0.0);
        return false;
    }
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            output[row * 3 + column] = inverse[row][column];
    return true;
}

inline void invert_affine(const double* input, double output[9]) {
    double determinant = input[0] * input[4] - input[1] * input[3];
    determinant = determinant != 0.0 ? 1.0 / determinant : 0.0;
    const double a11 = input[4] * determinant;
    const double a22 = input[0] * determinant;
    const double a12 = -input[1] * determinant;
    const double a21 = -input[3] * determinant;
    const double b1 = -a11 * input[2] - a12 * input[5];
    const double b2 = -a21 * input[2] - a22 * input[5];
    output[0] = a11;
    output[1] = a12;
    output[2] = b1;
    output[3] = a21;
    output[4] = a22;
    output[5] = b2;
    output[6] = 0.0;
    output[7] = 0.0;
    output[8] = 1.0;
}

struct WarpPerspectivePlan {
    std::array<double, 9> inverse{};
    bool valid = false;
};

struct WarpAffinePlan {
    std::array<double, 9> inverse{};
    std::vector<int> x_delta;
    std::vector<int> y_delta;
    std::vector<int> x_origin;
    std::vector<int> y_origin;
    bool valid = false;
};

inline bool prepareWarpPerspective(const Mat& transform,
                                   WarpPerspectivePlan& plan) {
    const double* forward = transform_values(transform);
    plan.valid = forward != nullptr;
    if (plan.valid)
        (void)invert_perspective(forward, plan.inverse.data());
    return plan.valid;
}

inline bool prepareWarpAffine(const Mat& transform, Size size,
                              WarpAffinePlan& plan) {
    const double* forward = transform_values(transform);
    plan.valid = forward != nullptr;
    if (!plan.valid) return false;
    invert_affine(forward, plan.inverse.data());
    constexpr int affine_scale = 1 << 10;
    plan.x_delta.resize(size.width);
    plan.y_delta.resize(size.width);
    plan.x_origin.resize(size.height);
    plan.y_origin.resize(size.height);
    for (int x = 0; x < size.width; ++x) {
        plan.x_delta[x] = round_to_int(
            plan.inverse[0] * x * affine_scale);
        plan.y_delta[x] = round_to_int(
            plan.inverse[3] * x * affine_scale);
    }
    for (int y = 0; y < size.height; ++y) {
        plan.x_origin[y] = round_to_int(
            (plan.inverse[1] * y + plan.inverse[2]) * affine_scale);
        plan.y_origin[y] = round_to_int(
            (plan.inverse[4] * y + plan.inverse[5]) * affine_scale);
    }
    return true;
}

inline const std::array<std::array<std::int16_t, 16>, 1024>&
warp_cubic_table() {
    static const auto table = [] {
        std::array<std::array<std::int16_t, 16>, 1024> result{};
        for (int fy = 0; fy < 32; ++fy) {
            float beta_float[4];
            cubic_coefficients_float(fy * (1.0f / 32.0f), beta_float);
            for (int fx = 0; fx < 32; ++fx) {
                float alpha_float[4];
                cubic_coefficients_float(fx * (1.0f / 32.0f), alpha_float);
                auto& weights = result[fy * 32 + fx];
                int sum = 0;
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x) {
                        const int weight = std::clamp(
                            round_to_int(beta_float[y] * alpha_float[x] *
                                         32768.0f),
                            -32768, 32767);
                        weights[y * 4 + x] =
                            static_cast<std::int16_t>(weight);
                        sum += weight;
                    }
                if (sum != 32768) {
                    const int difference = sum - 32768;
                    int minimum = 10;
                    int maximum = 10;
                    for (int y = 2; y < 4; ++y)
                        for (int x = 2; x < 4; ++x) {
                            const int index = y * 4 + x;
                            if (weights[index] < weights[minimum])
                                minimum = index;
                            else if (weights[index] > weights[maximum])
                                maximum = index;
                        }
                    const int corrected = difference < 0
                        ? weights[maximum] - difference
                        : weights[minimum] - difference;
                    weights[difference < 0 ? maximum : minimum] =
                        static_cast<std::int16_t>(corrected);
                }
            }
        }
        return result;
    }();
    return table;
}

inline std::uint8_t warp_source_value(const Mat& source, int x, int y,
                                      int channel, int channels) {
    if (x < 0 || x >= source.cols || y < 0 || y >= source.rows)
        return 0;
    return source.ptr(y)[x * channels + channel];
}

template <typename CoordinateCallback>
inline void warpRows(const Mat& source, Mat& destination, Size size,
                     int interpolation, int row_begin, int row_end,
                     CoordinateCallback&& coordinate) {
    const int channels = source.type() == CV_8UC1 ? 1 : 4;
    if (interpolation == INTER_AREA) interpolation = INTER_LINEAR;
    row_begin = std::clamp(row_begin, 0, size.height);
    row_end = std::clamp(row_end, row_begin, size.height);
    for (int y = row_begin; y < row_end; ++y) {
        auto* output = destination.ptr(y);
        for (int x = 0; x < size.width; ++x) {
            int fixed_x;
            int fixed_y;
            coordinate(x, y, interpolation != INTER_NEAREST,
                       fixed_x, fixed_y);
            if (interpolation == INTER_NEAREST) {
                const int source_x = fixed_x;
                const int source_y = fixed_y;
                if (source.type() == CV_8UC4 && source_x >= 0 &&
                    source_x < source.cols && source_y >= 0 &&
                    source_y < source.rows) {
                    std::uint32_t pixel;
                    std::memcpy(&pixel, source.ptr(source_y) + source_x * 4,
                                sizeof(pixel));
                    std::memcpy(output + x * 4, &pixel, sizeof(pixel));
                } else if (source.type() == CV_8UC1 && source_x >= 0 &&
                           source_x < source.cols && source_y >= 0 &&
                           source_y < source.rows) {
                    output[x] = source.ptr(source_y)[source_x];
                } else {
                    std::memset(output + x * channels, 0, channels);
                }
                continue;
            }

            const int source_x = fixed_x >> 5;
            const int source_y = fixed_y >> 5;
            const int fraction_x = fixed_x & 31;
            const int fraction_y = fixed_y & 31;
            if (interpolation == INTER_CUBIC) {
                const auto& weights =
                    warp_cubic_table()[fraction_y * 32 + fraction_x];
                for (int channel = 0; channel < channels; ++channel) {
                    int sum = 0;
                    for (int tap_y = 0; tap_y < 4; ++tap_y)
                        for (int tap_x = 0; tap_x < 4; ++tap_x)
                            sum += warp_source_value(
                                source, source_x - 1 + tap_x,
                                source_y - 1 + tap_y, channel, channels) *
                                weights[tap_y * 4 + tap_x];
                    output[x * channels + channel] =
                        static_cast<std::uint8_t>(std::clamp(
                            (sum + 16384) >> 15, 0, 255));
                }
                continue;
            }

            const int weight00 =
                (32 - fraction_x) * (32 - fraction_y) * 32;
            const int weight01 =
                fraction_x * (32 - fraction_y) * 32;
            const int weight10 =
                (32 - fraction_x) * fraction_y * 32;
            const int weight11 = fraction_x * fraction_y * 32;
            for (int channel = 0; channel < channels; ++channel) {
                const int sum =
                    warp_source_value(source, source_x, source_y,
                                      channel, channels) * weight00 +
                    warp_source_value(source, source_x + 1, source_y,
                                      channel, channels) * weight01 +
                    warp_source_value(source, source_x, source_y + 1,
                                      channel, channels) * weight10 +
                    warp_source_value(source, source_x + 1, source_y + 1,
                                      channel, channels) * weight11;
                output[x * channels + channel] =
                    static_cast<std::uint8_t>(std::clamp(
                        (sum + 16384) >> 15, 0, 255));
            }
        }
    }
}

inline void warpPerspectiveRows(const Mat& source, Mat& destination,
                                const WarpPerspectivePlan& plan, Size size,
                                int interpolation, int row_begin,
                                int row_end) {
    if (!plan.valid) {
        const std::size_t row_bytes = static_cast<std::size_t>(size.width) *
            (source.type() == CV_8UC1 ? 1 : 4);
        for (int row = std::clamp(row_begin, 0, size.height);
             row < std::clamp(row_end, 0, size.height); ++row)
            std::memset(destination.ptr(row), 0, row_bytes);
        return;
    }
    warpRows(source, destination, size, interpolation, row_begin, row_end,
             [&](int x, int y, bool fractional, int& output_x,
                 int& output_y) {
        const double denominator = plan.inverse[6] * x +
            plan.inverse[7] * y + plan.inverse[8];
        const double reciprocal = denominator
            ? (fractional ? 32.0 : 1.0) / denominator
            : 0.0;
        output_x = round_to_int(std::clamp(
            (plan.inverse[0] * x + plan.inverse[1] * y +
             plan.inverse[2]) * reciprocal,
            static_cast<double>(std::numeric_limits<int>::min()),
            static_cast<double>(std::numeric_limits<int>::max())));
        output_y = round_to_int(std::clamp(
            (plan.inverse[3] * x + plan.inverse[4] * y +
             plan.inverse[5]) * reciprocal,
            static_cast<double>(std::numeric_limits<int>::min()),
            static_cast<double>(std::numeric_limits<int>::max())));
    });
}

inline void warpAffineRows(const Mat& source, Mat& destination,
                           const WarpAffinePlan& plan, Size size,
                           int interpolation, int row_begin, int row_end) {
    if (!plan.valid) {
        const std::size_t row_bytes = static_cast<std::size_t>(size.width) *
            (source.type() == CV_8UC1 ? 1 : 4);
        for (int row = std::clamp(row_begin, 0, size.height);
             row < std::clamp(row_end, 0, size.height); ++row)
            std::memset(destination.ptr(row), 0, row_bytes);
        return;
    }
    constexpr int affine_scale = 1 << 10;
    warpRows(source, destination, size, interpolation, row_begin, row_end,
             [&](int x, int y, bool fractional, int& output_x,
                 int& output_y) {
        const int round_delta = fractional ? 16 : affine_scale / 2;
        const int shift = fractional ? 5 : 10;
        const auto shifted_coordinate = [shift](std::int64_t value) {
            const auto bounded = std::clamp(
                value,
                static_cast<std::int64_t>(
                    std::numeric_limits<int>::min()),
                static_cast<std::int64_t>(
                    std::numeric_limits<int>::max()));
            return static_cast<int>(bounded) >> shift;
        };
        output_x = shifted_coordinate(
            static_cast<std::int64_t>(plan.x_origin[y]) + round_delta +
            plan.x_delta[x]);
        output_y = shifted_coordinate(
            static_cast<std::int64_t>(plan.y_origin[y]) + round_delta +
            plan.y_delta[x]);
    });
}

inline void warpPerspective(const Mat& source, Mat& destination,
                            const Mat& transform, Size size,
                            int interpolation) {
    const bool aliases_source = destination.ptr() == source.ptr() &&
        destination.ptr() != nullptr;
    Mat separate_destination;
    Mat* output = &destination;
    const bool preserve_destination = aliases_source &&
        destination.rows == size.height && destination.cols == size.width &&
        destination.type() == source.type();
    if (aliases_source) {
        separate_destination.create(size.height, size.width, source.type());
        output = &separate_destination;
    } else if (!destination.ptr() || destination.rows != size.height ||
               destination.cols != size.width ||
               destination.type() != source.type()) {
        destination.create(size.height, size.width, source.type());
    }
    WarpPerspectivePlan plan;
    if (prepareWarpPerspective(transform, plan)) {
        warpPerspectiveRows(source, *output, plan, size, interpolation,
                            0, size.height);
    } else {
        for (int row = 0; row < size.height; ++row)
            std::memset(output->ptr(row), 0,
                        static_cast<std::size_t>(size.width) *
                            (source.type() == CV_8UC1 ? 1 : 4));
    }
    if (preserve_destination) {
        const std::size_t row_bytes = static_cast<std::size_t>(size.width) *
            (source.type() == CV_8UC1 ? 1 : 4);
        for (int row = 0; row < size.height; ++row)
            std::memcpy(destination.ptr(row), separate_destination.ptr(row),
                        row_bytes);
    } else if (aliases_source) {
        destination = std::move(separate_destination);
    }
}

inline void warpAffine(const Mat& source, Mat& destination,
                       const Mat& transform, Size size, int interpolation) {
    const bool aliases_source = destination.ptr() == source.ptr() &&
        destination.ptr() != nullptr;
    Mat separate_destination;
    Mat* output = &destination;
    const bool preserve_destination = aliases_source &&
        destination.rows == size.height && destination.cols == size.width &&
        destination.type() == source.type();
    if (aliases_source) {
        separate_destination.create(size.height, size.width, source.type());
        output = &separate_destination;
    } else if (!destination.ptr() || destination.rows != size.height ||
               destination.cols != size.width ||
               destination.type() != source.type()) {
        destination.create(size.height, size.width, source.type());
    }
    WarpAffinePlan plan;
    if (prepareWarpAffine(transform, size, plan)) {
        warpAffineRows(source, *output, plan, size, interpolation,
                       0, size.height);
    } else {
        for (int row = 0; row < size.height; ++row)
            std::memset(output->ptr(row), 0,
                        static_cast<std::size_t>(size.width) *
                            (source.type() == CV_8UC1 ? 1 : 4));
    }
    if (preserve_destination) {
        const std::size_t row_bytes = static_cast<std::size_t>(size.width) *
            (source.type() == CV_8UC1 ? 1 : 4);
        for (int row = 0; row < size.height; ++row)
            std::memcpy(destination.ptr(row), separate_destination.ptr(row),
                        row_bytes);
    } else if (aliases_source) {
        destination = std::move(separate_destination);
    }
}

} // namespace cv
