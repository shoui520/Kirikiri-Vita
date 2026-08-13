#include "ncbind/ncbind.hpp"

#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "tjsNative.h"
#include "tvpgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define NCB_MODULE_NAME TJS_W("perspective.dll")

namespace {

struct Point
{
	double X;
	double Y;
};

bool UnitSquareToQuad(const Point &p00, const Point &p10,
	const Point &p01, const Point &p11, double matrix[9])
{
	const double a0 = p10.X - p11.X;
	const double a1 = p01.X - p11.X;
	const double b0 = p10.Y - p11.Y;
	const double b1 = p01.Y - p11.Y;
	const double c0 = p11.X - p10.X - p01.X + p00.X;
	const double c1 = p11.Y - p10.Y - p01.Y + p00.Y;
	const double determinant = a0 * b1 - a1 * b0;

	double g = 0.0;
	double h = 0.0;
	if(std::abs(c0) > 1.0e-10 || std::abs(c1) > 1.0e-10)
	{
		if(std::abs(determinant) < 1.0e-12) return false;
		g = (c0 * b1 - a1 * c1) / determinant;
		h = (a0 * c1 - c0 * b0) / determinant;
	}

	matrix[0] = p10.X * (g + 1.0) - p00.X;
	matrix[1] = p01.X * (h + 1.0) - p00.X;
	matrix[2] = p00.X;
	matrix[3] = p10.Y * (g + 1.0) - p00.Y;
	matrix[4] = p01.Y * (h + 1.0) - p00.Y;
	matrix[5] = p00.Y;
	matrix[6] = g;
	matrix[7] = h;
	matrix[8] = 1.0;
	return true;
}

bool Invert3x3(const double source[9], double inverse[9])
{
	const double determinant =
		source[0] * (source[4] * source[8] - source[5] * source[7]) -
		source[1] * (source[3] * source[8] - source[5] * source[6]) +
		source[2] * (source[3] * source[7] - source[4] * source[6]);
	if(std::abs(determinant) < 1.0e-12) return false;
	const double scale = 1.0 / determinant;
	inverse[0] =  (source[4] * source[8] - source[5] * source[7]) * scale;
	inverse[1] = -(source[1] * source[8] - source[2] * source[7]) * scale;
	inverse[2] =  (source[1] * source[5] - source[2] * source[4]) * scale;
	inverse[3] = -(source[3] * source[8] - source[5] * source[6]) * scale;
	inverse[4] =  (source[0] * source[8] - source[2] * source[6]) * scale;
	inverse[5] = -(source[0] * source[5] - source[2] * source[3]) * scale;
	inverse[6] =  (source[3] * source[7] - source[4] * source[6]) * scale;
	inverse[7] = -(source[0] * source[7] - source[1] * source[6]) * scale;
	inverse[8] =  (source[0] * source[4] - source[1] * source[3]) * scale;
	return true;
}

tjs_uint32 BilinearSample(const std::vector<tjs_uint32> &pixels,
	tjs_int width, tjs_int height, double x, double y)
{
	x = std::max(0.0, std::min(x, static_cast<double>(width - 1)));
	y = std::max(0.0, std::min(y, static_cast<double>(height - 1)));
	const tjs_int x0 = static_cast<tjs_int>(std::floor(x));
	const tjs_int y0 = static_cast<tjs_int>(std::floor(y));
	const tjs_int x1 = std::min(x0 + 1, width - 1);
	const tjs_int y1 = std::min(y0 + 1, height - 1);
	const double fx = x - x0;
	const double fy = y - y0;
	const tjs_uint32 samples[4] = {
		pixels[static_cast<std::size_t>(y0) * width + x0],
		pixels[static_cast<std::size_t>(y0) * width + x1],
		pixels[static_cast<std::size_t>(y1) * width + x0],
		pixels[static_cast<std::size_t>(y1) * width + x1]
	};

	tjs_uint32 output = 0;
	for(unsigned shift = 0; shift < 32; shift += 8)
	{
		const double top = ((samples[0] >> shift) & 0xffu) * (1.0 - fx) +
			((samples[1] >> shift) & 0xffu) * fx;
		const double bottom = ((samples[2] >> shift) & 0xffu) * (1.0 - fx) +
			((samples[3] >> shift) & 0xffu) * fx;
		const auto channel = static_cast<tjs_uint32>(std::lround(
			top * (1.0 - fy) + bottom * fy));
		output |= std::min<tjs_uint32>(255, channel) << shift;
	}
	return output;
}

tjs_error TJS_INTF_METHOD PerspectiveCopy(tTJSVariant *, tjs_int numparams,
	tTJSVariant **param, iTJSDispatch2 *objthis)
{
	if(!objthis) return TJS_E_NATIVECLASSCRASH;
	if(numparams < 13) return TJS_E_BADPARAMCOUNT;

	tTJSNI_Layer *destination = nullptr;
	if(TJS_FAILED(objthis->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
		tTJSNC_Layer::ClassID,
		reinterpret_cast<iTJSNativeInstance **>(&destination))))
		return TJS_E_NATIVECLASSCRASH;

	tTJSNI_BaseLayer *source = nullptr;
	const tTJSVariantClosure closure = param[0]->AsObjectClosureNoAddRef();
	if(!closure.Object || TJS_FAILED(closure.Object->NativeInstanceSupport(
		TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
		reinterpret_cast<iTJSNativeInstance **>(&source))))
		TVPThrowExceptionMessage(TVPSpecifyLayer);

	tTVPBaseBitmap *source_bitmap = source->GetMainImage();
	tTVPBaseBitmap *destination_bitmap = destination->GetMainImage();
	if(!source_bitmap || !destination_bitmap ||
		!source_bitmap->Is32BPP() || !destination_bitmap->Is32BPP())
		TVPThrowExceptionMessage(TVPSpecifyLayer);

	const tjs_int source_left = *param[1];
	const tjs_int source_top = *param[2];
	const tjs_int source_width = *param[3];
	const tjs_int source_height = *param[4];
	if(source_width <= 0 || source_height <= 0) return TJS_S_OK;

	const Point points[4] = {
		{param[5]->AsReal(), param[6]->AsReal()},
		{param[7]->AsReal(), param[8]->AsReal()},
		{param[9]->AsReal(), param[10]->AsReal()},
		{param[11]->AsReal(), param[12]->AsReal()}
	};
	double forward[9];
	double inverse[9];
	if(!UnitSquareToQuad(points[0], points[1], points[2], points[3], forward) ||
		!Invert3x3(forward, inverse)) return TJS_S_OK;

	const tjs_int bitmap_width = static_cast<tjs_int>(destination_bitmap->GetWidth());
	const tjs_int bitmap_height = static_cast<tjs_int>(destination_bitmap->GetHeight());
	tjs_int left = bitmap_width;
	tjs_int top = bitmap_height;
	tjs_int right = 0;
	tjs_int bottom = 0;
	for(const Point &point : points)
	{
		left = std::min(left, static_cast<tjs_int>(std::floor(point.X)));
		top = std::min(top, static_cast<tjs_int>(std::floor(point.Y)));
		right = std::max(right, static_cast<tjs_int>(std::ceil(point.X)));
		bottom = std::max(bottom, static_cast<tjs_int>(std::ceil(point.Y)));
	}
	left = std::max(left, std::max(0, destination->GetClipLeft()));
	top = std::max(top, std::max(0, destination->GetClipTop()));
	right = std::min(right, std::min(bitmap_width,
		destination->GetClipLeft() + destination->GetClipWidth()));
	bottom = std::min(bottom, std::min(bitmap_height,
		destination->GetClipTop() + destination->GetClipHeight()));
	if(left >= right || top >= bottom) return TJS_S_OK;

	const tjs_int source_bitmap_width = static_cast<tjs_int>(source_bitmap->GetWidth());
	const tjs_int source_bitmap_height = static_cast<tjs_int>(source_bitmap->GetHeight());
	std::vector<tjs_uint32> source_pixels(
		static_cast<std::size_t>(source_bitmap_width) * source_bitmap_height);
	for(tjs_int y = 0; y < source_bitmap_height; ++y)
		std::memcpy(source_pixels.data() + static_cast<std::size_t>(y) * source_bitmap_width,
			source_bitmap->GetScanLine(y),
			static_cast<std::size_t>(source_bitmap_width) * sizeof(tjs_uint32));

	for(tjs_int y = top; y < bottom; ++y)
	{
		auto *destination_line = static_cast<tjs_uint32 *>(
			destination_bitmap->GetScanLineForWrite(y));
		for(tjs_int x = left; x < right; ++x)
		{
			const double px = x + 0.5;
			const double py = y + 0.5;
			const double divisor = inverse[6] * px + inverse[7] * py + inverse[8];
			if(std::abs(divisor) < 1.0e-12) continue;
			const double u = (inverse[0] * px + inverse[1] * py + inverse[2]) / divisor;
			const double v = (inverse[3] * px + inverse[4] * py + inverse[5]) / divisor;
			if(u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) continue;

			const double sx = source_left + u * (source_width - 1);
			const double sy = source_top + v * (source_height - 1);
			if(sx < 0.0 || sy < 0.0 || sx > source_bitmap_width - 1 ||
				sy > source_bitmap_height - 1) continue;
			const tjs_uint32 sample = BilinearSample(source_pixels,
				source_bitmap_width, source_bitmap_height, sx, sy);
			TVPAlphaBlend_a(destination_line + x, &sample, 1);
		}
	}

	destination->SetImageModified(true);
	destination->Update(tTVPRect(left, top, right, bottom));
	return TJS_S_OK;
}

void RegisterPerspective()
{
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	tTJSVariant layer_value;
	global->PropGet(0, TJS_W("Layer"), nullptr, &layer_value, global);
	iTJSDispatch2 *layer = layer_value.AsObjectNoAddRef();
	if(layer)
	{
		tTJSDispatch *method = TJSCreateNativeClassMethod(PerspectiveCopy);
		tTJSVariant value(method);
		method->Release();
		layer->PropSet(TJS_MEMBERENSURE, TJS_W("perspectiveCopy"), nullptr,
			&value, layer);
	}
	global->Release();
}

} // namespace

NCB_PRE_REGIST_CALLBACK(RegisterPerspective);
