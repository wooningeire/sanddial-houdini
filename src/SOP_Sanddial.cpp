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

// ── Viewport Mode ──────────────────────────────────────────────────────────
static PRM_Name prm_viewportModeName("viewport_mode", "Viewport Mode");
static PRM_Name prm_viewportModeChoices[] = {
    PRM_Name("view",              "View"),
    PRM_Name("erodibility_paint", "Erodibility Paint"),
    PRM_Name("environment_edit",  "Environment Edit"),
    PRM_Name(0)
};
static PRM_ChoiceList prm_viewportModeMenu(PRM_CHOICELIST_SINGLE,
                                           prm_viewportModeChoices);

// ── Material ───────────────────────────────────────────────────────────────
static PRM_Name    prm_weakErodName("weak_erodibility", "Weak Erodibility");
static PRM_Default prm_weakErodDefault(1.0);

static PRM_Name    prm_strongErodName("strong_erodibility", "Strong Erodibility");
static PRM_Default prm_strongErodDefault(0.2);

static PRM_Name    prm_stressThreshName("stress_threshold", "Stress Threshold");
static PRM_Default prm_stressThreshDefault(1000.0);

static PRM_Name    prm_youngModName("young_modulus", "Young's Modulus");
static PRM_Default prm_youngModDefault(1e5);

static PRM_Name    prm_poissonName("poisson_ratio", "Poisson's Ratio");
static PRM_Default prm_poissonDefault(0.3);
static PRM_Range   prm_poissonRange(PRM_RANGE_RESTRICTED, 0.0,
                                    PRM_RANGE_RESTRICTED, 0.499);

static PRM_Name    prm_plasticityName("plasticity_yield", "Plasticity Yield");
static PRM_Default prm_plasticityDefault(2500.0);

// ── Environment ────────────────────────────────────────────────────────────
static PRM_Name    prm_windDirName("wind_direction", "Wind Direction");
static PRM_Default prm_windDirDefaults[] = {
    PRM_Default(1.0), PRM_Default(0.0), PRM_Default(0.0)
};

static PRM_Name    prm_windSpeedName("wind_speed", "Wind Speed");
static PRM_Default prm_windSpeedDefault(5.0);

static PRM_Name    prm_turbulenceName("turbulence", "Turbulence");
static PRM_Default prm_turbulenceDefault(0.2);
static PRM_Range   prm_turbulenceRange(PRM_RANGE_RESTRICTED, 0.0,
                                       PRM_RANGE_RESTRICTED, 1.0);

static PRM_Name    prm_precipName("precipitation", "Precipitation");
static PRM_Default prm_precipDefault(1.0);

static PRM_Name    prm_critShearName("critical_shear_stress", "Critical Shear Stress");
static PRM_Default prm_critShearDefault(0.05);

// ── Simulation ─────────────────────────────────────────────────────────────
static PRM_Name    prm_timestepName("timestep", "Timestep");
static PRM_Default prm_timestepDefault(1.0);

static PRM_Name    prm_voxelSizeName("voxel_size", "Voxel Size");
static PRM_Default prm_voxelSizeDefault(0.2);

static PRM_Name    prm_domainSizeName("domain_size", "Domain Size");
static PRM_Default prm_domainSizeDefaults[] = {
    PRM_Default(10.0), PRM_Default(10.0), PRM_Default(10.0)
};

static PRM_Name prm_simStateName("sim_state", "Simulation State");
static PRM_Name prm_simStateChoices[] = {
    PRM_Name("locked", "Locked to Frame"),
    PRM_Name("live",   "Live"),
    PRM_Name(0)
};
static PRM_ChoiceList prm_simStateMenu(PRM_CHOICELIST_SINGLE,
                                       prm_simStateChoices);

static PRM_Name    prm_lockFrameName("lock_frame", "Lock Frame");
static PRM_Default prm_lockFrameDefault(1);

static int bakeCB(void* /*data*/, int /*index*/, fpreal64 /*time*/,
                  const PRM_Template* /*tplate*/) {
    // TODO: Implement bake functionality
    return 1;
}
static PRM_Name prm_bakeName("bake", "Bake");

// ── Meshing ────────────────────────────────────────────────────────────────
static PRM_Name    prm_poissonDepthName("poisson_depth", "Poisson Depth");
static PRM_Default prm_poissonDepthDefault(8);

static PRM_Name    prm_poissonScaleName("poisson_scale", "Poisson Scale");
static PRM_Default prm_poissonScaleDefault(1.1);

static PRM_Name    prm_subdivIterName("subdiv_iterations", "Subdivision Iterations");
static PRM_Default prm_subdivIterDefault(2);

// ── Folder Switcher ────────────────────────────────────────────────────────
static PRM_Name    prm_folderName("folder", "");
static PRM_Default prm_folderDefaults[] = {
    PRM_Default(6, "Material"),
    PRM_Default(5, "Environment"),
    PRM_Default(6, "Simulation"),
    PRM_Default(3, "Meshing"),
};

PRM_Template SOP_Sanddial::myTemplateList[] = {
    // Viewport mode selector (outside folders)
    PRM_Template(PRM_ORD, 1, &prm_viewportModeName, 0, &prm_viewportModeMenu),

    // Folder tabs
    PRM_Template(PRM_SWITCHER, 4, &prm_folderName, prm_folderDefaults),

    // ── Material (6 params) ────────────────────────────────────────────
    PRM_Template(PRM_FLT, 1, &prm_weakErodName,    &prm_weakErodDefault),
    PRM_Template(PRM_FLT, 1, &prm_strongErodName,   &prm_strongErodDefault),
    PRM_Template(PRM_FLT, 1, &prm_stressThreshName, &prm_stressThreshDefault),
    PRM_Template(PRM_FLT, 1, &prm_youngModName,     &prm_youngModDefault),
    PRM_Template(PRM_FLT, 1, &prm_poissonName,      &prm_poissonDefault,
                 0, &prm_poissonRange),
    PRM_Template(PRM_FLT, 1, &prm_plasticityName,   &prm_plasticityDefault),

    // ── Environment (5 params) ─────────────────────────────────────────
    PRM_Template(PRM_FLT_J, 3, &prm_windDirName,   prm_windDirDefaults),
    PRM_Template(PRM_FLT, 1, &prm_windSpeedName,    &prm_windSpeedDefault),
    PRM_Template(PRM_FLT, 1, &prm_turbulenceName,   &prm_turbulenceDefault,
                 0, &prm_turbulenceRange),
    PRM_Template(PRM_FLT, 1, &prm_precipName,       &prm_precipDefault),
    PRM_Template(PRM_FLT, 1, &prm_critShearName,    &prm_critShearDefault),

    // ── Simulation (6 params) ──────────────────────────────────────────
    PRM_Template(PRM_FLT, 1, &prm_timestepName,     &prm_timestepDefault),
    PRM_Template(PRM_FLT, 1, &prm_voxelSizeName,    &prm_voxelSizeDefault),
    PRM_Template(PRM_FLT_J, 3, &prm_domainSizeName, prm_domainSizeDefaults),
    PRM_Template(PRM_ORD, 1, &prm_simStateName, 0,  &prm_simStateMenu),
    PRM_Template(PRM_INT, 1, &prm_lockFrameName,    &prm_lockFrameDefault),
    PRM_Template(PRM_CALLBACK, 1, &prm_bakeName, 0, 0, 0, bakeCB),

    // ── Meshing (3 params) ─────────────────────────────────────────────
    PRM_Template(PRM_INT, 1, &prm_poissonDepthName,  &prm_poissonDepthDefault),
    PRM_Template(PRM_FLT, 1, &prm_poissonScaleName,  &prm_poissonScaleDefault),
    PRM_Template(PRM_INT, 1, &prm_subdivIterName,    &prm_subdivIterDefault),

    PRM_Template() // sentinel
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

// ── Simulation helpers ──────────────────────────────────────────────────────
void SOP_Sanddial::initializeSimulation(const GU_Detail* inputGeo) {
    // Populate AreniteGeometry from Houdini's input geometry.
    myGeo.initFromHoudiniGeo(inputGeo);

    // Initialize erodibility from normalized Y if the input didn't have it.
    if (!inputGeo->findPointAttribute("erodibility")) {
        fpreal yMin = 1e18, yMax = -1e18;
        for (const auto& p : myGeo.particles) {
            if (p.position.y() < yMin) yMin = p.position.y();
            if (p.position.y() > yMax) yMax = p.position.y();
        }
        fpreal yRange = (yMax - yMin);
        if (yRange < 1e-9) yRange = 1.0;
        for (auto& p : myGeo.particles) {
            p.erodibility = (p.position.y() - yMin) / yRange;
        }
    }

    // Set up the voxel grid.
    myGeo.initGrid();
}

void SOP_Sanddial::advanceFrame(fpreal dt) {
    // 0. Reset per-step accumulators and grid.
    myGeo.resetStepData();

    // 1. Compute stress tensors via MLS-MPM.
    myMpmSolver.solve(myGeo, dt);

    // 2. Recalculate particle normals.
    myNormalsSolver.solve(myGeo);

    // 3. Compute wind erosion (deflation + abrasion).
    myWindSolver.solve(myGeo, dt);

    // 4. Compute fluvial erosion (FastFlow).
    myWaterSolver.solve(myGeo, dt);

    // 5. Combine erosion and update viabilities.
    myErosionSolver.solve(myGeo, dt);

    // 6. Deposit eroded particles via gravity routing.
    myDepositionSolver.solve(myGeo, dt);
}

void SOP_Sanddial::loadParameters(fpreal t) {
    // Material
    myErosionSolver.weakErodibility   = evalFloat("weak_erodibility", 0, t);
    myErosionSolver.strongErodibility = evalFloat("strong_erodibility", 0, t);
    myErosionSolver.stressThreshold   = evalFloat("stress_threshold", 0, t);
    myMpmSolver.youngModulus          = evalFloat("young_modulus", 0, t);
    myMpmSolver.poissonRatio          = evalFloat("poisson_ratio", 0, t);

    // Environment
    myWindSolver.windDirection = UT_Vector3(
        evalFloat("wind_direction", 0, t),
        evalFloat("wind_direction", 1, t),
        evalFloat("wind_direction", 2, t));
    myWindSolver.windSpeed     = evalFloat("wind_speed", 0, t);
    myWindSolver.turbulence    = evalFloat("turbulence", 0, t);
    myWaterSolver.precipitation      = evalFloat("precipitation", 0, t);
    myWaterSolver.criticalShearStress = evalFloat("critical_shear_stress", 0, t);

    // Simulation
    myGeo.voxelSize = evalFloat("voxel_size", 0, t);
}

GU_DetailHandle SOP_Sanddial::getFrameResult(int frame, const GU_Detail* inputGeo, fpreal fps) {
    // Already cached?
    auto it = myFrameCache.find(frame);
    if (it != myFrameCache.end())
        return it->second;

    fpreal dt = 1.0 / fps;

    if (frame <= myStartFrame) {
        // Initialize from input
        initializeSimulation(inputGeo);
        GU_DetailHandle gdh;
        gdh.allocateAndSet(new GU_Detail());
        myGeo.writeToHoudiniGeo(gdh.gdpNC());
        myFrameCache[frame] = gdh;
        return gdh;
    }

    // Make sure previous frame is cached first
    getFrameResult(frame - 1, inputGeo, fps);

    // Advance the simulation one step
    advanceFrame(dt);

    // Write result to a new handle
    GU_DetailHandle gdh;
    gdh.allocateAndSet(new GU_Detail());
    myGeo.writeToHoudiniGeo(gdh.gdpNC());

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

    // ── Simulation locking ──────────────────────────────────────────────
    int simState = evalInt("sim_state", 0, t);
    if (simState == 0) { // Locked to Frame
        frame = evalInt("lock_frame", 0, t);
        if (frame < myStartFrame)
            frame = myStartFrame;
    }

    const GU_Detail* srcGeo = inputGeo(0, context);
    if (!srcGeo) {
        addError(SOP_MESSAGE, "No input geometry");
        return error();
    }

    // If input changed (topology, positions, attributes), invalidate cache
    GA_DataId currentDataId = srcGeo->getP()->getDataId();
    if (currentDataId != myInputDataId) {
        myFrameCache.clear();
        myInputDataId = currentDataId;
    }

    // Load Parameter Pane values into solvers.
    loadParameters(t);

    GU_DetailHandle result = getFrameResult(frame, srcGeo, fps);

    // Write result into gdp
    gdp->clearAndDestroy();
    const GU_Detail* resultGeo = result.gdp();
    if (resultGeo)
        gdp->copy(*resultGeo);

    // ── Viewport mode coloring ──────────────────────────────────────────
    int viewportMode = evalInt("viewport_mode", 0, t);
    if (viewportMode == 1) { // Erodibility Paint
        GA_ROHandleF erodH(gdp->findPointAttribute("erodibility"));
        if (erodH.isValid()) {
            GA_RWHandleV3 cdH(gdp->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
            if (cdH.isValid()) {
                GA_Offset ptoff;
                GA_FOR_ALL_PTOFF(gdp, ptoff) {
                    fpreal e = SYSclamp(erodH.get(ptoff), 0.0, 1.0);
                    // Blue (low erodibility / strong) → Red (high / weak)
                    UT_Vector3 color(e, 0.2 * (1.0 - e), 1.0 - e);
                    cdH.set(ptoff, color);
                }
            }
        }
    }

    return error();
}
