#include <opencv2/opencv.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <std::size_t Size>
void check_pixels(const cv::Mat& image,
                  const std::array<std::uint8_t, Size>& expected,
                  const std::string& label) {
    std::size_t index = 0;
    const int channels = image.type() == CV_8UC1 ? 1 : 4;
    for (int y = 0; y < image.rows; ++y) {
        const auto* row = image.ptr(y);
        for (int x = 0; x < image.cols * channels; ++x, ++index)
            if (row[x] != expected[index]) {
                check(false, label + " pixel " + std::to_string(index));
                return;
            }
    }
    check(index == Size, label + " expected-size contract");
}

void check_padding(const std::vector<std::uint8_t>& storage,
                   int rows, int columns, int pitch,
                   std::uint8_t sentinel, const std::string& label,
                   int offset = 0) {
    for (int y = 0; y < rows; ++y)
        for (int x = columns; x < pitch; ++x)
            check(storage[offset + y * pitch + x] == sentinel,
                  label + " row padding");
}

constexpr std::array<std::array<std::uint8_t, 20>, 4> resize_expected{{
    {{0, 0, 19, 38, 57, 0, 0, 19, 38, 57,
      76, 76, 95, 114, 133, 152, 152, 171, 190, 209}},
    {{0, 13, 28, 43, 57, 48, 61, 76, 91, 104,
      105, 118, 133, 148, 161, 152, 165, 180, 195, 209}},
    {{0, 6, 23, 40, 53, 39, 52, 69, 86, 98,
      111, 123, 140, 157, 170, 156, 169, 186, 203, 216}},
    {{0, 14, 29, 43, 57, 51, 65, 79, 93, 108,
      101, 115, 130, 144, 158, 152, 166, 181, 195, 209}},
}};

constexpr std::array<std::array<std::uint8_t, 20>, 4> perspective_expected{{
    {{0, 19, 38, 57, 0, 0, 19, 114, 133, 0,
      0, 95, 114, 209, 0, 0, 152, 190, 209, 0}},
    {{0, 8, 22, 34, 0, 15, 46, 75, 104, 0,
      42, 113, 141, 170, 0, 40, 120, 119, 108, 14}},
    {{0, 0, 14, 30, 0, 9, 40, 70, 103, 0,
      44, 137, 163, 215, 0, 40, 144, 131, 130, 13}},
    {{0, 8, 22, 34, 0, 15, 46, 75, 104, 0,
      42, 113, 141, 170, 0, 40, 120, 119, 108, 14}},
}};

constexpr std::array<std::array<std::uint8_t, 20>, 4> affine_expected{{
    {{0, 0, 38, 57, 0, 0, 19, 114, 133, 0,
      0, 95, 114, 209, 0, 0, 152, 190, 209, 0}},
    {{0, 8, 22, 35, 0, 15, 46, 72, 101, 0,
      43, 113, 141, 170, 11, 39, 115, 119, 114, 18}},
    {{0, 0, 14, 29, 0, 9, 40, 68, 102, 0,
      46, 137, 163, 217, 10, 38, 138, 131, 139, 17}},
    {{0, 8, 22, 35, 0, 15, 46, 72, 101, 0,
      43, 113, 141, 170, 11, 39, 115, 119, 114, 18}},
}};

void test_resize_modes_and_row_tasks() {
    constexpr int source_pitch = 7;
    constexpr int destination_pitch = 9;
    std::vector<std::uint8_t> source_storage(3 * source_pitch, 0xee);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            source_storage[y * source_pitch + x] =
                static_cast<std::uint8_t>((y * 4 + x) * 19);
    cv::Mat source(3, 4, CV_8UC1, source_storage.data(), source_pitch);

    for (int interpolation = cv::INTER_NEAREST;
         interpolation <= cv::INTER_AREA; ++interpolation) {
        std::vector<std::uint8_t> destination_storage(
            4 * destination_pitch, 0xa5);
        cv::Mat destination(4, 5, CV_8UC1,
                            destination_storage.data(), destination_pitch);
        auto* external_pointer = destination.ptr();
        cv::resize(source, destination, cv::Size(5, 4), 0, 0,
                   interpolation);
        check(destination.ptr() == external_pointer,
              "resize keeps external destination");
        check_pixels(destination, resize_expected[interpolation],
                     "resize mode " + std::to_string(interpolation));
        check_padding(destination_storage, 4, 5, destination_pitch, 0xa5,
                      "resize mode " + std::to_string(interpolation));

        std::fill(destination_storage.begin(), destination_storage.end(),
                  0xa5);
        cv::ResizeWorkspace workspace;
        std::array<cv::ResizeTaskWorkspace, 3> task_workspaces;
        cv::prepareResizeWorkspace(source, cv::Size(5, 4), interpolation,
                                   workspace);
        for (auto& task_workspace : task_workspaces)
            cv::prepareResizeTaskWorkspace(
                source, cv::Size(5, 4), interpolation, task_workspace);
        const auto area_capacity = task_workspaces[0].area_rows.capacity();
        const auto cubic_capacity = task_workspaces[0].cubic_rows.capacity();
        for (int lane = 0; lane < 3; ++lane)
            cv::resizeRows(source, destination, cv::Size(5, 4),
                           interpolation, workspace, task_workspaces[lane],
                           4 * lane / 3, 4 * (lane + 1) / 3);
        check_pixels(destination, resize_expected[interpolation],
                     "row-split resize mode " +
                         std::to_string(interpolation));
        check(task_workspaces[0].area_rows.capacity() == area_capacity,
              "AREA worker scratch does not grow during dispatch");
        check(task_workspaces[0].cubic_rows.capacity() == cubic_capacity,
              "CUBIC worker scratch does not grow during dispatch");
    }
}

void test_unaligned_rgba_nearest() {
    constexpr int source_pitch = 17;
    constexpr int destination_pitch = 23;
    std::vector<std::uint8_t> source_storage(1 + 2 * source_pitch, 0xee);
    std::vector<std::uint8_t> destination_storage(
        1 + 3 * destination_pitch, 0xa5);
    auto* source_pixels = source_storage.data() + 1;
    auto* destination_pixels = destination_storage.data() + 1;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            for (int channel = 0; channel < 4; ++channel)
                source_pixels[y * source_pitch + x * 4 + channel] =
                    static_cast<std::uint8_t>(y * 80 + x * 16 + channel);
    cv::Mat source(2, 3, CV_8UC4, source_pixels, source_pitch);
    cv::Mat destination(3, 5, CV_8UC4, destination_pixels,
                        destination_pitch);
    cv::resize(source, destination, cv::Size(5, 3), 0, 0,
               cv::INTER_NEAREST);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) {
            const int source_y = y * 2 / 3;
            const int source_x = x * 3 / 5;
            check(std::memcmp(
                      destination.ptr(y) + x * 4,
                      source.ptr(source_y) + source_x * 4, 4) == 0,
                  "unaligned RGBA nearest mapping");
        }
    check_padding(destination_storage, 3, 20, destination_pitch, 0xa5,
                  "unaligned RGBA nearest", 1);
}

void test_resize_reciprocal_boundary_mapping() {
    std::array<std::uint8_t, 14> source_storage{};
    for (int x = 0; x < 14; ++x)
        source_storage[x] = static_cast<std::uint8_t>(x);
    std::array<std::uint8_t, 18> destination_storage{};
    cv::Mat source(1, 14, CV_8UC1, source_storage.data());
    cv::Mat destination(1, 18, CV_8UC1, destination_storage.data());

    cv::resize(source, destination, cv::Size(18, 1), 0, 0,
               cv::INTER_NEAREST);
    check(destination_storage[9] == 6,
          "nearest uses OpenCV reciprocal scale evaluation order");
    cv::resize(source, destination, cv::Size(18, 1), 0, 0,
               cv::INTER_AREA);
    check(destination_storage[9] == 6,
          "AREA uses OpenCV reciprocal scale evaluation order");
}

void test_in_place_resize_lifetime() {
    constexpr int source_pitch = 7;
    std::vector<std::uint8_t> external_storage(3 * source_pitch, 0xee);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            external_storage[y * source_pitch + x] =
                static_cast<std::uint8_t>((y * 4 + x) * 19);
    const auto original_external = external_storage;
    cv::Mat external(3, 4, CV_8UC1, external_storage.data(), source_pitch);
    auto* external_pointer = external.ptr();
    cv::resize(external, external, cv::Size(5, 4), 0, 0,
               cv::INTER_NEAREST);
    check(external.ptr() != external_pointer,
          "size-changing external resize allocates destination");
    check(external_storage == original_external,
          "size-changing external resize preserves source backing");
    check_pixels(external, resize_expected[cv::INTER_NEAREST],
                 "size-changing external in-place resize");

    cv::Mat owned;
    owned.create(3, 4, CV_8UC1);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            owned.ptr(y)[x] = static_cast<std::uint8_t>((y * 4 + x) * 19);
    cv::resize(owned, owned, cv::Size(5, 4), 0, 0,
               cv::INTER_NEAREST);
    check_pixels(owned, resize_expected[cv::INTER_NEAREST],
                 "size-changing owned in-place resize");

    std::array<std::uint8_t, 12> shared_storage{};
    for (int index = 0; index < 12; ++index)
        shared_storage[index] = static_cast<std::uint8_t>(index * 19);
    auto shared_source_copy = shared_storage;
    cv::Mat shared_source(3, 4, CV_8UC1, shared_storage.data(), 4);
    cv::Mat shared_destination(2, 6, CV_8UC1, shared_storage.data(), 6);
    cv::Mat distinct_source(
        3, 4, CV_8UC1,
        shared_source_copy.data(), 4);
    cv::Mat expected;
    cv::resize(distinct_source, expected, cv::Size(6, 2), 0, 0,
               cv::INTER_NEAREST);
    auto* shared_pointer = shared_destination.ptr();
    cv::resize(shared_source, shared_destination, cv::Size(6, 2), 0, 0,
               cv::INTER_NEAREST);
    check(shared_destination.ptr() == shared_pointer,
          "different-shaped same-pointer resize retains destination");
    for (int row = 0; row < 2; ++row)
        check(std::memcmp(shared_destination.ptr(row), expected.ptr(row), 6) ==
                  0,
              "different-shaped same-pointer resize pixels");
}

void test_area_downscale_paths() {
    {
        constexpr int source_pitch = 9;
        constexpr int destination_pitch = 7;
        std::vector<std::uint8_t> source_storage(5 * source_pitch, 0xee);
        std::vector<std::uint8_t> destination_storage(
            3 * destination_pitch, 0xa5);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) {
                const int index = y * 7 + x;
                source_storage[y * source_pitch + x] =
                    static_cast<std::uint8_t>(
                        (index * 37 + (index / 5) * 11 + 7) & 255);
            }
        cv::Mat source(5, 7, CV_8UC1, source_storage.data(), source_pitch);
        cv::Mat destination(3, 4, CV_8UC1, destination_storage.data(),
                            destination_pitch);
        cv::resize(source, destination, cv::Size(4, 3), 0, 0,
                   cv::INTER_AREA);
        check_pixels(destination,
                     std::array<std::uint8_t, 12>{
                         28, 93, 161, 230, 56, 124,
                         183, 119, 84, 153, 184, 30},
                     "fractional AREA downscale C1");
        check_padding(destination_storage, 3, 4, destination_pitch, 0xa5,
                      "fractional AREA downscale C1");
    }

    {
        constexpr int source_pitch = 6 * 4 + 3;
        constexpr int destination_pitch = 3 * 4 + 5;
        std::vector<std::uint8_t> source_storage(4 * source_pitch, 0xee);
        std::vector<std::uint8_t> destination_storage(
            2 * destination_pitch, 0xa5);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 6; ++x)
                for (int channel = 0; channel < 4; ++channel) {
                    const int index = (y * 6 + x) * 4 + channel;
                    source_storage[y * source_pitch + x * 4 + channel] =
                        static_cast<std::uint8_t>(
                            (index * 37 + (index / 5) * 11 + 7) & 255);
                }
        cv::Mat source(4, 6, CV_8UC4, source_storage.data(), source_pitch);
        cv::Mat destination(2, 3, CV_8UC4, destination_storage.data(),
                            destination_pitch);
        cv::ResizeWorkspace workspace;
        std::array<cv::ResizeTaskWorkspace, 3> task_workspaces;
        cv::prepareResizeWorkspace(source, cv::Size(3, 2), cv::INTER_AREA,
                                   workspace);
        for (auto& task_workspace : task_workspaces)
            cv::prepareResizeTaskWorkspace(
                source, cv::Size(3, 2), cv::INTER_AREA, task_workspace);
        for (int lane = 0; lane < 3; ++lane)
            cv::resizeRows(source, destination, cv::Size(3, 2),
                           cv::INTER_AREA, workspace,
                           task_workspaces[lane], 2 * lane / 3,
                           2 * (lane + 1) / 3);
        check_pixels(destination,
                     std::array<std::uint8_t, 24>{
                         102, 144, 120, 93, 161, 134, 110, 152,
                         92, 132, 105, 142, 129, 102, 142, 120,
                         124, 100, 137, 110, 114, 154, 132, 172},
                     "integer 2x2 AREA downscale pitched C4");
        check_padding(destination_storage, 2, 12, destination_pitch, 0xa5,
                      "integer 2x2 AREA downscale pitched C4");
    }

    {
        constexpr int source_pitch = 7 * 4 + 3;
        constexpr int destination_pitch = 4 * 4 + 5;
        std::vector<std::uint8_t> source_storage(5 * source_pitch, 0xee);
        std::vector<std::uint8_t> destination_storage(
            3 * destination_pitch, 0xa5);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x)
                for (int channel = 0; channel < 4; ++channel) {
                    const int index = (y * 7 + x) * 4 + channel;
                    source_storage[y * source_pitch + x * 4 + channel] =
                        static_cast<std::uint8_t>(
                            (index * 37 + (index / 5) * 11 + 7) & 255);
                }
        cv::Mat source(5, 7, CV_8UC4, source_storage.data(), source_pitch);
        cv::Mat destination(3, 4, CV_8UC4, destination_storage.data(),
                            destination_pitch);
        cv::resize(source, destination, cv::Size(4, 3), 0, 0,
                   cv::INTER_AREA);
        check_pixels(destination,
                     std::array<std::uint8_t, 48>{
                         99, 95, 135, 108, 119, 143, 139, 157,
                         133, 172, 107, 146, 137, 119, 70, 110,
                         130, 117, 156, 107, 134, 167, 104, 143,
                         89, 120, 116, 134, 140, 126, 166, 109,
                         136, 117, 91, 131, 129, 124, 111, 150,
                         155, 136, 148, 98, 161, 156, 107, 89},
                     "fractional AREA downscale pitched C4");
        check_padding(destination_storage, 3, 16, destination_pitch, 0xa5,
                      "fractional AREA downscale pitched C4");
    }
}

void test_box_filter() {
    constexpr int source_pitch = 7;
    constexpr int destination_pitch = 9;
    std::vector<std::uint8_t> source_storage(3 * source_pitch, 0xee);
    std::vector<std::uint8_t> destination_storage(
        3 * destination_pitch, 0xa5);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            source_storage[y * source_pitch + x] =
                static_cast<std::uint8_t>((y * 4 + x) * 19);
    cv::Mat source(3, 4, CV_8UC1, source_storage.data(), source_pitch);
    cv::Mat destination(3, 4, CV_8UC1, destination_storage.data(),
                        destination_pitch);
    cv::BoxFilterWorkspace workspace;
    cv::boxFilter(source, destination, -1, cv::Size(3, 3), workspace);
    check_pixels(destination,
                 std::array<std::uint8_t, 12>{
                     63, 70, 89, 95, 89, 95,
                     114, 120, 114, 120, 139, 146},
                 "3x3 REFLECT_101 box filter");
    const auto capacity = workspace.column_sums.capacity();
    cv::boxFilter(source, destination, -1, cv::Size(4, 2), workspace);
    check_pixels(destination,
                 std::array<std::uint8_t, 12>{
                     57, 57, 67, 76, 57, 57,
                     67, 76, 133, 133, 143, 152},
                 "even-kernel box filter");
    check(workspace.column_sums.capacity() == capacity,
          "box-filter workspace is reused");
    check_padding(destination_storage, 3, 4, destination_pitch, 0xa5,
                  "box filter");

    constexpr int alias_pitch = 7;
    std::vector<std::uint8_t> alias_storage(5 * alias_pitch, 0xee);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 4; ++x)
            alias_storage[y * alias_pitch + x] =
                static_cast<std::uint8_t>((y * 4 + x) * 9);
    cv::Mat alias(5, 4, CV_8UC1, alias_storage.data(), alias_pitch);
    cv::boxFilter(alias, alias, -1, cv::Size(3, 3), workspace);
    check_pixels(alias,
                 std::array<std::uint8_t, 20>{
                     30, 33, 42, 45, 42, 45, 54, 57, 78, 81,
                     90, 93, 114, 117, 126, 129, 126, 129, 138, 141},
                 "in-place REFLECT_101 box filter");
    check_padding(alias_storage, 5, 4, alias_pitch, 0xee,
                  "in-place box filter");
    check(workspace.delayed_rows.size() == 2 * 4,
          "in-place box filter retains only top+1 rows");

    std::uint8_t one_pixel = 1;
    std::uint8_t one_pixel_result = 0;
    cv::Mat one_pixel_source(1, 1, CV_8UC1, &one_pixel);
    cv::Mat one_pixel_destination(1, 1, CV_8UC1, &one_pixel_result);
    cv::boxFilter(one_pixel_source, one_pixel_destination, -1,
                  cv::Size(2, 1), workspace);
    check(one_pixel_result == 2,
          "OpenCV 16-bit even-area box divider contract");

    std::array<std::uint8_t, 4> one_rgba{{1, 1, 1, 1}};
    std::array<std::uint8_t, 4> one_rgba_result{};
    cv::Mat one_rgba_source(1, 1, CV_8UC4, one_rgba.data());
    cv::Mat one_rgba_destination(
        1, 1, CV_8UC4, one_rgba_result.data());
    cv::boxFilter(one_rgba_source, one_rgba_destination, -1,
                  cv::Size(2, 1), workspace);
    check(one_rgba_result == std::array<std::uint8_t, 4>{{2, 2, 2, 2}},
          "OpenCV RGBA even-area box divider contract");

    // OpenCV 4.7's 128-bit NEON ColumnSum path processes eight flattened
    // channel elements with float normalization, then finishes the scalar
    // tail with double normalization.  Three RGBA pixels exercise both paths
    // for a divisor above the 16-bit fixed-divider range.
    std::array<std::uint8_t, 48> wide_box_source{{
        3, 17, 29, 41, 53, 67, 79, 83, 97, 109, 127, 131,
        139, 149, 157, 163, 173, 181, 191, 199, 211, 223, 227, 233,
        239, 251, 7, 19, 31, 43, 59, 71, 89, 101, 113, 137,
        151, 167, 179, 193, 197, 213, 229, 241, 5, 23, 37, 47}};
    std::array<std::uint8_t, 48> wide_box_destination{};
    cv::Mat wide_box_source_mat(
        4, 3, CV_8UC4, wide_box_source.data());
    cv::Mat wide_box_destination_mat(
        4, 3, CV_8UC4, wide_box_destination.data());
    cv::boxFilter(wide_box_source_mat, wide_box_destination_mat, -1,
                  cv::Size(17, 17), workspace);
    check_pixels(wide_box_destination_mat,
                 std::array<std::uint8_t, 48>{{
                     123, 135, 120, 130, 120, 132, 123, 133,
                     121, 133, 124, 134, 125, 137, 127, 137,
                     123, 135, 129, 139, 123, 135, 129, 140,
                     121, 133, 119, 129, 118, 130, 122, 132,
                     119, 131, 122, 132, 128, 140, 126, 136,
                     125, 137, 128, 139, 126, 138, 128, 139}},
                 "large-area RGBA mixed NEON/scalar box divider contract");
}

void test_degenerate_transform_coefficients() {
    const cv::Point2f perspective_source[4] = {
        {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};
    const cv::Point2f perspective_destination[4] = {
        {0.0f, 0.0f}, {3.0f, 0.0f}, {3.0f, 3.0f}, {0.0f, 3.0f}};
    const cv::Mat perspective = cv::getPerspectiveTransform(
        perspective_source, perspective_destination);
    const double* perspective_values = cv::transform_values(perspective);
    check(perspective_values != nullptr,
          "degenerate perspective returns coefficients");
    for (int index = 0; index < 8; ++index)
        check(perspective_values[index] == 0.0,
              "degenerate perspective coefficient is zero");
    check(perspective_values[8] == 1.0,
          "degenerate perspective homogeneous coefficient is one");

    const cv::Point2f affine_source[3] = {
        {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};
    const cv::Point2f affine_destination[3] = {
        {0.0f, 0.0f}, {3.0f, 0.0f}, {3.0f, 3.0f}};
    const cv::Mat affine = cv::getAffineTransform(
        affine_source, affine_destination);
    const double* affine_values = cv::transform_values(affine);
    check(affine_values != nullptr,
          "degenerate affine returns coefficients");
    for (int index = 0; index < 6; ++index)
        check(affine_values[index] == 0.0,
              "degenerate affine coefficient is zero");
}

void test_transform_solver_coefficients() {
    const cv::Point2f source[4] = {
        {0.0f, 0.0f}, {1279.0f, 0.0f},
        {1279.0f, 959.0f}, {0.0f, 959.0f}};
    const cv::Point2f destination[4] = {
        {12.25f, 5.75f}, {1100.5f, 30.125f},
        {1175.75f, 850.875f}, {50.5f, 900.25f}};
    constexpr std::array<double, 9> perspective_expected_coefficients{{
        0x1.da331e734e503p-1, 0x1.3607e385dab3ep-5,
        0x1.88p+3, 0x1.5a054569ec53ap-6,
        0x1.caf264b0ff67ap-1, 0x1.7p+2,
        0x1.1f080cf36cbd8p-14, -0x1.52d2b89037df2p-15,
        0x1p+0}};
    constexpr std::array<double, 6> affine_expected_coefficients{{
        0x1.b3a3ed95eac89p-1, 0x1.4166c612afa65p-4,
        0x1.88p+3, 0x1.383e72e3c727dp-6,
        0x1.b630957d3273ep-1, 0x1.7p+2}};

    const cv::Mat perspective =
        cv::getPerspectiveTransform(source, destination);
    const double* perspective_values = cv::transform_values(perspective);
    for (std::size_t index = 0;
         index < perspective_expected_coefficients.size(); ++index)
        check(perspective_values[index] ==
                  perspective_expected_coefficients[index],
              "perspective LU coefficient " + std::to_string(index));

    const cv::Mat affine = cv::getAffineTransform(source, destination);
    const double* affine_values = cv::transform_values(affine);
    for (std::size_t index = 0;
         index < affine_expected_coefficients.size(); ++index)
        check(affine_values[index] == affine_expected_coefficients[index],
              "affine LU coefficient " + std::to_string(index));
}

void test_in_place_warp_lifetime() {
    constexpr int pitch = 7;
    std::vector<std::uint8_t> source_storage(3 * pitch, 0xee);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            source_storage[y * pitch + x] =
                static_cast<std::uint8_t>(7 + (y * 4 + x) * 17);
    const cv::Mat affine = cv::make_transform(
        {{0.95, 0.08, 0.2, -0.04, 1.02, 0.3, 0.0, 0.0, 1.0}});
    const cv::Mat perspective = cv::make_transform(
        {{0.95, 0.08, 0.2, -0.04, 1.02, 0.3,
          0.004, -0.003, 1.0}});

    for (int kind = 0; kind < 2; ++kind) {
        std::vector<std::uint8_t> expected_storage(3 * pitch, 0xa5);
        cv::Mat source(3, 4, CV_8UC1, source_storage.data(), pitch);
        cv::Mat expected(3, 4, CV_8UC1,
                         expected_storage.data(), pitch);
        if (kind == 0)
            cv::warpAffine(source, expected, affine, cv::Size(4, 3),
                           cv::INTER_LINEAR);
        else
            cv::warpPerspective(source, expected, perspective,
                                cv::Size(4, 3), cv::INTER_LINEAR);

        std::vector<std::uint8_t> alias_storage = source_storage;
        cv::Mat alias(3, 4, CV_8UC1, alias_storage.data(), pitch);
        auto* external_pointer = alias.ptr();
        if (kind == 0)
            cv::warpAffine(alias, alias, affine, cv::Size(4, 3),
                           cv::INTER_LINEAR);
        else
            cv::warpPerspective(alias, alias, perspective,
                                cv::Size(4, 3), cv::INTER_LINEAR);
        check(alias.ptr() == external_pointer,
              "in-place warp retains external destination");
        for (int y = 0; y < 3; ++y)
            check(std::memcmp(alias.ptr(y), expected.ptr(y), 4) == 0,
                  "in-place warp pixels");
        check_padding(alias_storage, 3, 4, pitch, 0xee,
                      "in-place warp");
    }
}

void test_near_singular_affine_bounds() {
    std::array<std::uint8_t, 16> source_storage{};
    std::array<std::uint8_t, 16> destination_storage{};
    for (int index = 0; index < 16; ++index)
        source_storage[index] = static_cast<std::uint8_t>(index + 7);
    cv::Mat source(4, 4, CV_8UC1, source_storage.data());
    cv::Mat destination(4, 4, CV_8UC1, destination_storage.data());
    const cv::Mat near_singular = cv::make_transform(
        {{1e-6, 0.0, -10.0, 0.0, 1e-6, -10.0, 0.0, 0.0, 1.0}});
    cv::warpAffine(source, destination, near_singular,
                   cv::Size(4, 4), cv::INTER_LINEAR);
    check(std::all_of(destination_storage.begin(), destination_storage.end(),
                      [](std::uint8_t value) { return value == 0; }),
          "near-singular affine coordinates remain bounded");
}

void test_warp_modes_and_external_destination() {
    constexpr int source_pitch = 7;
    constexpr int destination_pitch = 9;
    std::vector<std::uint8_t> source_storage(3 * source_pitch, 0xee);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            source_storage[y * source_pitch + x] =
                static_cast<std::uint8_t>((y * 4 + x) * 19);
    cv::Mat source(3, 4, CV_8UC1, source_storage.data(), source_pitch);
    const cv::Mat perspective = cv::make_transform(
        {{0.88, 0.12, 0.35, -0.08, 1.07, 0.6,
          0.006, -0.004, 1.0}});
    const cv::Mat affine = cv::make_transform(
        {{0.88, 0.12, 0.35, -0.08, 1.07, 0.6, 0.0, 0.0, 1.0}});

    for (int interpolation = cv::INTER_NEAREST;
         interpolation <= cv::INTER_AREA; ++interpolation) {
        std::vector<std::uint8_t> destination_storage(
            4 * destination_pitch, 0xa5);
        cv::Mat destination(4, 5, CV_8UC1,
                            destination_storage.data(), destination_pitch);
        auto* external_pointer = destination.ptr();
        cv::warpPerspective(source, destination, perspective,
                            cv::Size(5, 4), interpolation);
        check(destination.ptr() == external_pointer,
              "perspective keeps external destination");
        check_pixels(destination, perspective_expected[interpolation],
                     "perspective mode " +
                         std::to_string(interpolation));
        check_padding(destination_storage, 4, 5, destination_pitch, 0xa5,
                      "perspective");

        std::fill(destination_storage.begin(), destination_storage.end(),
                  0xa5);
        cv::WarpPerspectivePlan perspective_plan;
        check(cv::prepareWarpPerspective(perspective, perspective_plan),
              "perspective row plan is invertible");
        for (int lane = 0; lane < 3; ++lane)
            cv::warpPerspectiveRows(
                source, destination, perspective_plan, cv::Size(5, 4),
                interpolation, 4 * lane / 3, 4 * (lane + 1) / 3);
        check_pixels(destination, perspective_expected[interpolation],
                     "row-split perspective mode " +
                         std::to_string(interpolation));

        std::fill(destination_storage.begin(), destination_storage.end(),
                  0xa5);
        cv::warpAffine(source, destination, affine, cv::Size(5, 4),
                       interpolation);
        check(destination.ptr() == external_pointer,
              "affine keeps external destination");
        check_pixels(destination, affine_expected[interpolation],
                     "affine mode " + std::to_string(interpolation));
        check_padding(destination_storage, 4, 5, destination_pitch, 0xa5,
                      "affine");

        std::fill(destination_storage.begin(), destination_storage.end(),
                  0xa5);
        cv::WarpAffinePlan affine_plan;
        check(cv::prepareWarpAffine(affine, cv::Size(5, 4), affine_plan),
              "affine row plan is invertible");
        const auto x_capacity = affine_plan.x_delta.capacity();
        for (int lane = 0; lane < 3; ++lane)
            cv::warpAffineRows(
                source, destination, affine_plan, cv::Size(5, 4),
                interpolation, 4 * lane / 3, 4 * (lane + 1) / 3);
        check_pixels(destination, affine_expected[interpolation],
                     "row-split affine mode " +
                         std::to_string(interpolation));
        check(cv::prepareWarpAffine(affine, cv::Size(5, 4), affine_plan),
              "affine row plan remains invertible");
        check(affine_plan.x_delta.capacity() == x_capacity,
              "affine plan reuses coordinate storage");
    }

    std::vector<std::uint8_t> destination_storage(
        4 * destination_pitch, 0xa5);
    cv::Mat destination(4, 5, CV_8UC1,
                        destination_storage.data(), destination_pitch);
    const cv::Mat singular = cv::make_transform(
        {{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}});
    std::vector<std::uint8_t> singular_source_storage = source_storage;
    singular_source_storage[0] = 7;
    cv::Mat singular_source(3, 4, CV_8UC1,
                            singular_source_storage.data(), source_pitch);
    cv::warpPerspective(singular_source, destination, singular,
                        cv::Size(5, 4), cv::INTER_LINEAR);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
            check(destination.ptr(y)[x] == 7,
                  "singular perspective samples source origin");
    check_padding(destination_storage, 4, 5, destination_pitch, 0xa5,
                  "singular perspective");

    std::fill(destination_storage.begin(), destination_storage.end(), 0xa5);
    cv::warpAffine(singular_source, destination, singular,
                   cv::Size(5, 4), cv::INTER_LINEAR);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
            check(destination.ptr(y)[x] == 7,
                  "singular affine samples source origin");
    check_padding(destination_storage, 4, 5, destination_pitch, 0xa5,
                  "singular affine");
}

}  // namespace

int main() {
    test_resize_modes_and_row_tasks();
    test_unaligned_rgba_nearest();
    test_resize_reciprocal_boundary_mapping();
    test_in_place_resize_lifetime();
    test_area_downscale_paths();
    test_box_filter();
    test_degenerate_transform_coefficients();
    test_transform_solver_coefficients();
    test_in_place_warp_lifetime();
    test_near_singular_affine_bounds();
    test_warp_modes_and_external_destination();
    if (failures != 0) {
        std::cerr << failures << " OpenCV compatibility checks failed\n";
        return 1;
    }
    std::cout << "Yuri OpenCV compatibility checks passed\n";
    return 0;
}
