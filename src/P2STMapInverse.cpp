// P2STMapInverse.cpp
// Generates a STABILIZE STMap from a position pass (P) using camera matrices.
//
// This is the warp-inverse of P2STMap (matchmove), but it is NOT produced by
// inverting the matchmove STMap per-pixel (that would be a scatter problem).
// Instead it uses the exact same projection chain as P2STMap, with two changes
// in how it is driven:
//
//   Matchmove (P2STMap):   ST(x,y) = project_ref(P_current(x,y))
//                          -> P animated, camera held at reference frame
//                          -> apply STMap to the clean/reference still.
//
//   Stabilize (this node): ST(x,y) = project_current(P_ref(x,y))
//                          -> P held at reference frame, camera animated
//                          -> apply STMap to the MOVING plate.
//
// The projection math is identical (normalize . format . projection . transform^-1 . P).
// The stabilize behaviour comes from holding the P input at a reference frame
// while the camera animates. That hold is built in via the "reference frame"
// knob (set "hold P at reference frame" off if you prefer an external FrameHold).
//
// Wiring:  P pass -> input 0 (img),  animated (live) camera -> input 1 (cam),
//          then STMap this node's output onto the moving plate.

static const char* const HELP =
    "Generates a STABILIZE STMap from a position pass (P) using camera matrices.\n"
    "Holds the P pass at a reference frame and projects through the animated\n"
    "camera, so applying the result (via an STMap node) to the moving plate\n"
    "warps the surface to sit still in the reference view.\n"
    "Warp-inverse of P2STMap (matchmove).\n";

#include "DDImage/PixelIop.h"
#include "DDImage/CameraOp.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/Matrix4.h"
#include "DDImage/OutputContext.h"

using namespace DD::Image;

class P2STMapInverse : public PixelIop
{
    Matrix4 _cam_transform_inv;
    Matrix4 _cam_projection;
    float _format_width;
    float _format_height;

    int  _reference_frame;   // frame at which the P pass is held
    bool _hold_reference;    // if false, P is read at current frame (use external FrameHold)

public:
    P2STMapInverse(Node* node) : PixelIop(node),
        _format_width(1.0f),
        _format_height(1.0f),
        _reference_frame(1001),
        _hold_reference(true)
    {
        _cam_transform_inv.makeIdentity();
        _cam_projection.makeIdentity();
    }

    bool pass_transform() const { return true; }

    virtual int minimum_inputs() const { return 2; } // img + cam
    virtual int maximum_inputs() const { return 2; }

    virtual void knobs(Knob_Callback f);

    static const Iop::Description d;
    const char* Class() const { return d.name; }
    const char* node_help() const { return HELP; }

    void _validate(bool);
    void _request(int x, int y, int r, int t, ChannelMask channels, int count);

    void in_channels(int input, ChannelSet& mask) const {
        if (input == 0) {
            mask += Mask_RGBA;
        }
    }

    void pixel_engine(const Row& in, int y, int x, int r, ChannelMask channels, Row& out);

    // Hold the P input (input 0) at the reference frame while leaving the
    // camera (input 1) animated. This is what turns the forward projection
    // into a stabilize map without needing an external FrameHold.
    const OutputContext& inputContext(int n, int offset, OutputContext& context) const
    {
        context = outputContext();
        if (_hold_reference && n == 0) {
            context.setFrame(static_cast<double>(_reference_frame));
        }
        return context;
    }

    bool test_input(int n, Op* op) const {
        if (n >= 1) {
            return dynamic_cast<CameraOp*>(op) != 0;
        }
        return Iop::test_input(n, op);
    }

    Op* default_input(int input) const {
        if (input == 1) {
            return CameraOp::default_camera();
        }
        return Iop::default_input(input);
    }

    const char* input_label(int input, char* buffer) const {
        switch (input) {
            case 0: return "P";
            case 1: return "cam";
        }
        return nullptr;
    }
};

void P2STMapInverse::_validate(bool for_real)
{
    copy_info();

    // Camera is read at the CURRENT (output) frame -> animated.
    CameraOp* cam_op = dynamic_cast<CameraOp*>(Op::input(1));
    if (cam_op) {
        cam_op->validate(for_real);

        _cam_transform_inv = cam_op->matrix().inverse();
        _cam_projection    = cam_op->projection();

        const Format& fmt = info_.format();
        _format_width  = static_cast<float>(fmt.width());
        _format_height = static_cast<float>(fmt.height());
    } else {
        _cam_transform_inv.makeIdentity();
        _cam_projection.makeIdentity();
        _format_width  = 1.0f;
        _format_height = 1.0f;
    }

    set_out_channels(Mask_RGBA);
    info_.turn_on(Mask_RGBA);
    info_.black_outside(true);
}

void P2STMapInverse::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{
    // Request RGBA from input (position pass). The input is pulled at the
    // held reference frame because of the inputContext() override above.
    ChannelSet request_chans = Mask_RGBA;
    input0().request(x, y, r, t, request_chans, count);
}

void P2STMapInverse::pixel_engine(const Row& in, int y, int x, int r, ChannelMask channels, Row& out)
{
    if (aborted())
        return;

    // Format matrix at the current (animated) frame.
    Matrix4 cam_format;
    cam_format.makeIdentity();
    CameraOp* cam_op = dynamic_cast<CameraOp*>(Op::input(1));
    if (cam_op) {
        cam_op->to_format(cam_format, &info_.format());
    }

    // Input channels (position pass P, held at reference frame).
    const float* R = in[Chan_Red];
    const float* G = in[Chan_Green];
    const float* B = in[Chan_Blue];
    const float* A = in[Chan_Alpha];

    // Output channels (STMap).
    float* outR = out.writable(Chan_Red);
    float* outG = out.writable(Chan_Green);
    float* outB = out.writable(Chan_Blue);
    float* outA = out.writable(Chan_Alpha);

    for (int X = x; X < r; X++) {
        Vector4 P(R[X], G[X], B[X], A[X]);

        // Step 1: world -> camera (NO w_divide)
        Vector4 v1 = _cam_transform_inv.transform(P);

        // Step 2: projection WITH w_divide
        Vector4 v2 = _cam_projection.transform(v1);
        if (v2.w != 0.0f) {
            v2 /= v2.w;
        }

        // Step 3: format WITH w_divide
        Vector4 v3 = cam_format.transform(v2);
        if (v3.w != 0.0f) {
            v3 /= v3.w;
        }

        // Step 4: normalize to 0..1 ST
        outR[X] = v3.x / _format_width;
        outG[X] = v3.y / _format_height;
        outB[X] = v3.z;   // kept for round-tripping / depth
        outA[X] = v3.w;
    }
}

void P2STMapInverse::knobs(Knob_Callback f)
{
    Int_knob(f, &_reference_frame, "reference_frame", "reference frame");
    Tooltip(f, "Frame at which the P pass is held. The camera stays animated, "
               "so the output STMap, applied to the moving plate, warps the "
               "surface back to its position in this reference frame.");

    Bool_knob(f, &_hold_reference, "hold_reference", "hold P at reference frame");
    Tooltip(f, "On: the P input is internally held at the reference frame.\n"
               "Off: P is read at the current frame (wire your own FrameHold "
               "on the P pass instead).");

    Divider(f, "");
    Text_knob(f, "P2STMapInverse by Peter Mercell 2025\nStabilize companion to P2STMap. Inspired by Ivan Busquets's C44Matrix");
}

static Iop* build(Node* node) {
    return new P2STMapInverse(node);
}

const Iop::Description P2STMapInverse::d("P2STMapInverse", "Transform/P2STMapInverse", build);