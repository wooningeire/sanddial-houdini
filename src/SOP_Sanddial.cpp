#include "SOP_Sanddial.h"

#include <GU/GU_Detail.h>
#include <GA/GA_Handle.h>
#include <GA/GA_Iterator.h>
#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>
#include <OP/OP_AutoLockInputs.h>
#include <OP/OP_Director.h>
#include <PRM/PRM_Include.h>
#include <UT/UT_Vector3.h>
#include <CH/CH_Manager.h>

// ── Parameters ──────────────────────────────────────────────────────────────
static PRM_Name prm_iterationsName("iterations", "Iterations");
static PRM_Default prm_iterationsDefault(10);

PRM_Template SOP_Sanddial::myTemplateList[] = {
    PRM_Template(PRM_INT, 1, &prm_iterationsName, &prm_iterationsDefault),
    PRM_Template()
};

// ── Construction / destruction ──────────────────────────────────────────────
OP_Node* SOP_Sanddial::myConstructor(OP_Network* net, const char* name, OP_Operator* op) {
    return new SOP_Sanddial(net, name, op);
}

SOP_Sanddial::SOP_Sanddial(OP_Network* net, const char* name, OP_Operator* op)
    : SOP_Node(net, name, op) {
    mySopFlags.setNeedGuide1(true);
}

SOP_Sanddial::~SOP_Sanddial() {}

// ── Particle helpers ────────────────────────────────────────────────────────
void SOP_Sanddial::initializeParticles(const GU_Detail* inputGeo, GU_Detail* outGeo) {
    outGeo->clearAndDestroy();

    // Copy every point from the input
    GA_Offset srcPt, dstPt;
    GA_FOR_ALL_PTOFF(inputGeo, srcPt) {
        dstPt = outGeo->appendPoint();
        outGeo->setPos3(dstPt, inputGeo->getPos3(srcPt));
    }

    // Add a velocity attribute, initialized to zero
    GA_RWHandleV3 velH(outGeo->addFloatTuple(GA_ATTRIB_POINT, "v", 3));
    if (velH.isValid()) {
        GA_FOR_ALL_PTOFF(outGeo, dstPt) {
            velH.set(dstPt, UT_Vector3(0, 0, 0));
        }
    }
}

void SOP_Sanddial::advanceFrame(GU_Detail* geo, fpreal dt) {
    const fpreal gravity = -9.81;

    GA_RWHandleV3 velH(geo->findPointAttribute("v"));
    if (!velH.isValid()) return;

    GA_Offset ptoff;
    GA_FOR_ALL_PTOFF(geo, ptoff) {
        UT_Vector3 vel = velH.get(ptoff);
        vel.y() += gravity * dt;
        velH.set(ptoff, vel);

        UT_Vector3 pos = geo->getPos3(ptoff);
        pos += vel * dt;
        geo->setPos3(ptoff, pos);
    }
}

GU_DetailHandle SOP_Sanddial::getFrameResult(int frame, const GU_Detail* inputGeo, fpreal fps) {
    // Already cached?
    auto it = myFrameCache.find(frame);
    if (it != myFrameCache.end())
        return it->second;

    fpreal dt = 1.0 / fps;

    if (frame <= myStartFrame) {
        // Initialize from input
        GU_DetailHandle gdh;
        gdh.allocateAndSet(new GU_Detail());
        initializeParticles(inputGeo, gdh.gdpNC());
        myFrameCache[frame] = gdh;
        return gdh;
    }

    // Make sure previous frame is cached first
    GU_DetailHandle prevHandle = getFrameResult(frame - 1, inputGeo, fps);

    // Clone previous frame and advance
    GU_DetailHandle gdh;
    gdh.allocateAndSet(new GU_Detail());
    gdh.gdpNC()->copy(*prevHandle.gdp());
    advanceFrame(gdh.gdpNC(), dt);

    myFrameCache[frame] = gdh;
    return gdh;
}

// ── Cook ────────────────────────────────────────────────────────────────────
OP_ERROR SOP_Sanddial::cookMySop(OP_Context& context) {
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
        return error();

    fpreal t = context.getTime();

    // Tell Houdini this node depends on the current frame so it re-cooks
    // when the playbar moves.
    flags().setTimeDep(true);

    fpreal fps = OPgetDirector()->getChannelManager()->getSamplesPerSec();
    int frame = (int)SYSrint(t * fps) + 1; // 1-based frame number

    const GU_Detail* srcGeo = inputGeo(0, context);
    if (!srcGeo) {
        addError(SOP_MESSAGE, "No input geometry");
        return error();
    }

    // If input changed (e.g. topology), invalidate cache
    auto startIt = myFrameCache.find(myStartFrame);
    if (startIt != myFrameCache.end()) {
        const GU_Detail* cachedStart = startIt->second.gdp();
        if (cachedStart && cachedStart->getNumPoints() != srcGeo->getNumPoints())
            myFrameCache.clear();
    }

    GU_DetailHandle result = getFrameResult(frame, srcGeo, fps);

    // Write result into gdp
    gdp->clearAndDestroy();
    const GU_Detail* resultGeo = result.gdp();
    if (resultGeo)
        gdp->copy(*resultGeo);

    return error();
}
