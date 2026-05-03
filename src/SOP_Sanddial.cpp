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
#include <SYS/SYS_Math.h>

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

static PRM_Name prm_visualizeModeName("visualize_mode", "Visualization");
static PRM_Name prm_visualizeModeChoices[] = {
    PRM_Name("nothing",     "Nothing"),
    PRM_Name("erodibility", "Erodibility"),
    PRM_Name("viability",   "Viability"),
    PRM_Name("stress",      "Stress"),
    PRM_Name("normals",     "Normals"),
    PRM_Name("deflation",   "Wind Deflation"),
    PRM_Name("abrasion",    "Wind Abrasion"),
    PRM_Name("water",       "Water"),
    PRM_Name("total_erosion", "Total Erosion"),
    PRM_Name(0)
};
static PRM_ChoiceList prm_visualizeModeMenu(PRM_CHOICELIST_SINGLE,
                                            prm_visualizeModeChoices);

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

static PRM_Name    prm_densityName("density", "Density");
static PRM_Default prm_densityDefault(2000.0);

static PRM_Name    prm_sedimentViabName("sediment_viability", "Sediment Viability");
static PRM_Default prm_sedimentViabDefault(0.05);

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

static PRM_Name    prm_abrasionCoeffName("abrasion_coeff", "Abrasion Coefficient");
static PRM_Default prm_abrasionCoeffDefault(0.1);

static PRM_Name    prm_smoothLengthName("smoothing_length", "Smoothing Length");
static PRM_Default prm_smoothLengthDefault(0.4);

static PRM_Name    prm_deflationCoeffName("deflation_coeff", "Deflation Coefficient");
static PRM_Default prm_deflationCoeffDefault(5.0);

static PRM_Name    prm_cohesionName("cohesion", "Cohesion");
static PRM_Default prm_cohesionDefault(1e6);

static PRM_Name    prm_frictionCoeffName("friction_coeff", "Friction Coefficient");
static PRM_Default prm_frictionCoeffDefault(0.75);

static PRM_Name    prm_windAlphaName("wind_alpha", "Wind Alpha");
static PRM_Default prm_windAlphaDefault(2e6);

static PRM_Name    prm_showWindName("show_wind", "Show Wind Particles");
static PRM_Default prm_showWindDefault(1);

// ── Simulation ─────────────────────────────────────────────────────────────
static PRM_Name    prm_timestepName("timestep", "Timestep");
static PRM_Default prm_timestepDefault(1.0);

static PRM_Name    prm_voxelSizeName("voxel_size", "Voxel Size");
static PRM_Default prm_voxelSizeDefault(0.2);

static PRM_Name    prm_normalRadiusName("normal_radius", "Normal Radius");
static PRM_Default prm_normalRadiusDefault(2.0);
static PRM_Range   prm_normalRadiusRange(PRM_RANGE_RESTRICTED, 0.5,
                                         PRM_RANGE_UI, 10.0);

static PRM_Name    prm_cflFactorName("cfl_factor", "CFL Factor");
static PRM_Default prm_cflFactorDefault(0.4);

static PRM_Name    prm_stableSlopeName("stable_slope", "Stable Slope");
static PRM_Default prm_stableSlopeDefault(0.5);

static PRM_Name    prm_domainSizeName("domain_size", "Domain Size");
static PRM_Default prm_domainSizeDefaults[] = {
    PRM_Default(1.0), PRM_Default(1.0), PRM_Default(1.0)
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
static PRM_Range   prm_lockFrameRange(PRM_RANGE_RESTRICTED, 1.0,
                                      PRM_RANGE_UI, 500.0);

static int bakeCB(void* data, int /*index*/, fpreal64 time,
                  const PRM_Template* /*tplate*/) {
    SOP_Sanddial* node = static_cast<SOP_Sanddial*>(static_cast<OP_Node*>(data));
    return node->performBake((fpreal)time);
}
static PRM_Name prm_bakeName("bake", "Bake");

static int resetBakeCB(void* data, int /*index*/, fpreal64 /*time*/,
                       const PRM_Template* /*tplate*/) {
    SOP_Sanddial* node = static_cast<SOP_Sanddial*>(static_cast<OP_Node*>(data));
    return node->performResetBake();
}
static PRM_Name prm_resetBakeName("reset_bake", "Reset Bake");

// ── Meshing ────────────────────────────────────────────────────────────────
static PRM_Name    prm_poissonDepthName("poisson_depth", "Poisson Depth");
static PRM_Default prm_poissonDepthDefault(8);

static PRM_Name    prm_poissonScaleName("poisson_scale", "Poisson Scale");
static PRM_Default prm_poissonScaleDefault(1.1);

static PRM_Name    prm_subdivIterName("subdiv_iterations", "Subdivision Iterations");
static PRM_Default prm_subdivIterDefault(2);

// ── Brush ──────────────────────────────────────────────────────────────────
static PRM_Name    prm_brushActiveName("brush_active", "Apply Brush");
static PRM_Default prm_brushActiveDefault(0);

static PRM_Name    prm_brushPosName("brush_pos", "Brush Position");
static PRM_Default prm_brushPosDefaults[] = {
    PRM_Default(0.0), PRM_Default(0.0), PRM_Default(0.0)
};

static PRM_Name    prm_brushRadiusName("brush_radius", "Brush Radius");
static PRM_Default prm_brushRadiusDefault(0.5);
static PRM_Range   prm_brushRadiusRange(PRM_RANGE_RESTRICTED, 0.001,
                                        PRM_RANGE_UI, 5.0);

static PRM_Name    prm_brushStrengthName("brush_strength", "Brush Strength");
static PRM_Default prm_brushStrengthDefault(0.1);
static PRM_Range   prm_brushStrengthRange(PRM_RANGE_RESTRICTED, 0.0,
                                          PRM_RANGE_RESTRICTED, 1.0);

static PRM_Name    prm_brushFalloffName("brush_falloff", "Brush Falloff");
static PRM_Default prm_brushFalloffDefault(2.0);
static PRM_Range   prm_brushFalloffRange(PRM_RANGE_RESTRICTED, 0.01,
                                         PRM_RANGE_UI, 5.0);

static PRM_Name prm_brushModeName("brush_mode", "Brush Mode");
static PRM_Name prm_brushModeChoices[] = {
    PRM_Name("add",       "Add"),
    PRM_Name("subtract",  "Subtract"),
    PRM_Name("overwrite", "Overwrite"),
    PRM_Name(0)
};
static PRM_ChoiceList prm_brushModeMenu(PRM_CHOICELIST_SINGLE,
                                        prm_brushModeChoices);

static PRM_Name    prm_brushSurfaceOnlyName("brush_surface_only", "Surface Only");
static PRM_Default prm_brushSurfaceOnlyDefault(0);

static PRM_Name    prm_brushDepthEnabledName("brush_depth_enabled", "Depth Limit");
static PRM_Default prm_brushDepthEnabledDefault(0);

static PRM_Name    prm_brushDepthName("brush_depth", "Max Depth");
static PRM_Default prm_brushDepthDefault(0.2);
static PRM_Range   prm_brushDepthRange(PRM_RANGE_RESTRICTED, 0.0,
                                      PRM_RANGE_UI, 2.0);

static PRM_Name    prm_brushDepthFalloffName("brush_depth_falloff", "Depth Falloff");
static PRM_Default prm_brushDepthFalloffDefault(1.0);
static PRM_Range   prm_brushDepthFalloffRange(PRM_RANGE_RESTRICTED, 0.01,
                                             PRM_RANGE_UI, 5.0);

static PRM_Name    prm_brushViewDirName("brush_view_dir", "View Direction");
static PRM_Default prm_brushViewDirDefaults[] = {
    PRM_Default(0.0), PRM_Default(0.0), PRM_Default(-1.0)
};

static PRM_Name prm_brushDepthModeName("brush_depth_mode", "Depth Alignment");
static PRM_Name prm_brushDepthModeChoices[] = {
    PRM_Name("normal", "Surface Normal"),
    PRM_Name("view",   "View Aligned"),
    PRM_Name(0)
};
static PRM_ChoiceList prm_brushDepthModeMenu(PRM_CHOICELIST_SINGLE,
                                            prm_brushDepthModeChoices);

// ── Folder Switcher ────────────────────────────────────────────────────────
static PRM_Name    prm_folderName("folder", "");
static PRM_Default prm_folderDefaults[] = {
    PRM_Default(8, "Material"),
    PRM_Default(11, "Environment"),
    PRM_Default(10, "Simulation"),
    PRM_Default(3, "Meshing"),
    PRM_Default(12, "Brush"),
};

PRM_Template SOP_Sanddial::myTemplateList[] = {
    // Viewport mode selector (outside folders)
    PRM_Template(PRM_ORD, 1, &prm_viewportModeName, 0, &prm_viewportModeMenu),
    PRM_Template(PRM_ORD, 1, &prm_visualizeModeName, 0, &prm_visualizeModeMenu),
    PRM_Template(PRM_TOGGLE, 1, &prm_showWindName, &prm_showWindDefault),

    // Folder tabs  (5 tabs now)
    PRM_Template(PRM_SWITCHER, 5, &prm_folderName, prm_folderDefaults),

    // ── Material (6 params) ────────────────────────────────────────────
    PRM_Template(PRM_FLT, 1, &prm_weakErodName,    &prm_weakErodDefault),
    PRM_Template(PRM_FLT, 1, &prm_strongErodName,   &prm_strongErodDefault),
    PRM_Template(PRM_FLT, 1, &prm_stressThreshName, &prm_stressThreshDefault),
    PRM_Template(PRM_FLT, 1, &prm_youngModName,     &prm_youngModDefault),
    PRM_Template(PRM_FLT, 1, &prm_poissonName,      &prm_poissonDefault,
                 0, &prm_poissonRange),
    PRM_Template(PRM_FLT, 1, &prm_plasticityName,   &prm_plasticityDefault),
    PRM_Template(PRM_FLT, 1, &prm_densityName,      &prm_densityDefault),
    PRM_Template(PRM_FLT, 1, &prm_sedimentViabName, &prm_sedimentViabDefault),

    // ── Environment (12 params) ─────────────────────────────────────────
    PRM_Template(PRM_FLT_J, 3, &prm_windDirName,   prm_windDirDefaults),
    PRM_Template(PRM_FLT, 1, &prm_windSpeedName,    &prm_windSpeedDefault),
    PRM_Template(PRM_FLT, 1, &prm_turbulenceName,   &prm_turbulenceDefault,
                 0, &prm_turbulenceRange),
    PRM_Template(PRM_FLT, 1, &prm_precipName,       &prm_precipDefault),
    PRM_Template(PRM_FLT, 1, &prm_critShearName,    &prm_critShearDefault),
    PRM_Template(PRM_FLT, 1, &prm_abrasionCoeffName, &prm_abrasionCoeffDefault),
    PRM_Template(PRM_FLT, 1, &prm_smoothLengthName,  &prm_smoothLengthDefault),
    PRM_Template(PRM_FLT, 1, &prm_deflationCoeffName, &prm_deflationCoeffDefault),
    PRM_Template(PRM_FLT, 1, &prm_cohesionName,      &prm_cohesionDefault),
    PRM_Template(PRM_FLT, 1, &prm_frictionCoeffName, &prm_frictionCoeffDefault),
    PRM_Template(PRM_FLT, 1, &prm_windAlphaName,     &prm_windAlphaDefault),

    // ── Simulation (10 params) ──────────────────────────────────────────
    PRM_Template(PRM_FLT, 1, &prm_timestepName,     &prm_timestepDefault),
    PRM_Template(PRM_FLT, 1, &prm_voxelSizeName,    &prm_voxelSizeDefault),
    PRM_Template(PRM_FLT, 1, &prm_normalRadiusName,  &prm_normalRadiusDefault,
                 0, &prm_normalRadiusRange),
    PRM_Template(PRM_FLT_J, 3, &prm_domainSizeName, prm_domainSizeDefaults),
    PRM_Template(PRM_FLT, 1, &prm_cflFactorName,     &prm_cflFactorDefault),
    PRM_Template(PRM_FLT, 1, &prm_stableSlopeName,   &prm_stableSlopeDefault),
    PRM_Template(PRM_ORD, 1, &prm_simStateName, 0,  &prm_simStateMenu),
    PRM_Template(PRM_INT, 1, &prm_lockFrameName,    &prm_lockFrameDefault,
                 0, &prm_lockFrameRange),
    PRM_Template(PRM_CALLBACK, 1, &prm_bakeName, 0, 0, 0, bakeCB),
    PRM_Template(PRM_CALLBACK, 1, &prm_resetBakeName, 0, 0, 0, resetBakeCB),

    // ── Meshing (3 params) ─────────────────────────────────────────────
    PRM_Template(PRM_INT, 1, &prm_poissonDepthName,  &prm_poissonDepthDefault),
    PRM_Template(PRM_FLT, 1, &prm_poissonScaleName,  &prm_poissonScaleDefault),
    PRM_Template(PRM_INT, 1, &prm_subdivIterName,    &prm_subdivIterDefault),

    // ── Brush (6 params) ───────────────────────────────────────────────
    PRM_Template(PRM_TOGGLE, 1, &prm_brushActiveName, &prm_brushActiveDefault),
    PRM_Template(PRM_FLT_J, 3, &prm_brushPosName,    prm_brushPosDefaults),
    PRM_Template(PRM_FLT, 1, &prm_brushRadiusName,   &prm_brushRadiusDefault,
                 0, &prm_brushRadiusRange),
    PRM_Template(PRM_FLT, 1, &prm_brushStrengthName, &prm_brushStrengthDefault,
                 0, &prm_brushStrengthRange),
    PRM_Template(PRM_FLT, 1, &prm_brushFalloffName,  &prm_brushFalloffDefault,
                 0, &prm_brushFalloffRange),
    PRM_Template(PRM_ORD, 1, &prm_brushModeName,     0, &prm_brushModeMenu),
    PRM_Template(PRM_TOGGLE, 1, &prm_brushSurfaceOnlyName, &prm_brushSurfaceOnlyDefault),
    PRM_Template(PRM_TOGGLE, 1, &prm_brushDepthEnabledName, &prm_brushDepthEnabledDefault),
    PRM_Template(PRM_FLT, 1, &prm_brushDepthName,    &prm_brushDepthDefault,
                 0, &prm_brushDepthRange),
    PRM_Template(PRM_FLT, 1, &prm_brushDepthFalloffName, &prm_brushDepthFalloffDefault,
                 0, &prm_brushDepthFalloffRange),
    PRM_Template(PRM_ORD, 1, &prm_brushDepthModeName, 0, &prm_brushDepthModeMenu),
    PRM_Template(PRM_FLT_J, 3, &prm_brushViewDirName, prm_brushViewDirDefaults),

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

    // Compute initial normals for surface reconstruction.
    myNormalsSolver.solve(myGeo);

    // Reset transient solver states.
    myWindSolver.reset();
}

void SOP_Sanddial::advanceFrame(fpreal dt, int frame) {
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
    myDepositionSolver.solve(myGeo, dt, frame);
}

void SOP_Sanddial::loadParameters(fpreal t) {
    // Material
    myErosionSolver.weakErodibility   = evalFloat("weak_erodibility", 0, t);
    myErosionSolver.strongErodibility = evalFloat("strong_erodibility", 0, t);
    myErosionSolver.stressThreshold   = evalFloat("stress_threshold", 0, t);
    myMpmSolver.youngModulus          = evalFloat("young_modulus", 0, t);
    myMpmSolver.poissonRatio          = evalFloat("poisson_ratio", 0, t);
    myMpmSolver.plasticityYield       = evalFloat("plasticity_yield", 0, t);
    myMpmSolver.density               = evalFloat("density", 0, t);
    myDepositionSolver.sedimentViability = evalFloat("sediment_viability", 0, t);

    // Environment
    myWindSolver.windDirection = UT_Vector3(
        evalFloat("wind_direction", 0, t),
        evalFloat("wind_direction", 1, t),
        evalFloat("wind_direction", 2, t));
    myWindSolver.windSpeed            = evalFloat("wind_speed", 0, t);
    myWindSolver.turbulence           = evalFloat("turbulence", 0, t);
    myWaterSolver.precipitation       = evalFloat("precipitation", 0, t);
    myWaterSolver.criticalShearStress = evalFloat("critical_shear_stress", 0, t);
    myWindSolver.abrasionCoeff        = evalFloat("abrasion_coeff", 0, t);
    myWindSolver.smoothingLength      = evalFloat("smoothing_length", 0, t);
    myWindSolver.deflationCoeff       = evalFloat("deflation_coeff", 0, t);
    myWindSolver.cohesion             = evalFloat("cohesion", 0, t);
    myWindSolver.frictionCoeff        = evalFloat("friction_coeff", 0, t);
    myWindSolver.windAlpha            = evalFloat("wind_alpha", 0, t);

    // Simulation
    mySimTimestep = evalFloat("timestep", 0, t);
    myGeo.voxelSize = evalFloat("voxel_size", 0, t);
    myNormalsSolver.smoothingRadiusMult = evalFloat("normal_radius", 0, t);
    myGeo.domainPadding = UT_Vector3(
        evalFloat("domain_size", 0, t),
        evalFloat("domain_size", 1, t),
        evalFloat("domain_size", 2, t));
    myMpmSolver.cflFactor = evalFloat("cfl_factor", 0, t);
    myDepositionSolver.stableSlopeThreshold = evalFloat("stable_slope", 0, t);

    // Meshing
    myPoissonDepth     = evalInt("poisson_depth", 0, t);
    myPoissonScale     = evalFloat("poisson_scale", 0, t);
    mySubdivIterations = evalInt("subdiv_iterations", 0, t);
}

// ── Bake / Reset Bake ───────────────────────────────────────────────────────
int SOP_Sanddial::performBake(fpreal t) {
    // Only bake when simulation is locked
    int simState = evalInt("sim_state", 0, t);
    if (simState != 0) // 0 == "Locked to Frame"
        return 0;

    int lockFrame = evalInt("lock_frame", 0, t);
    if (lockFrame < myStartFrame)
        lockFrame = myStartFrame;

    // Find the cached geometry for the locked frame
    auto it = myFrameCache.find(lockFrame);
    if (it == myFrameCache.end())
        return 0; // frame not yet simulated

    // Push current state onto the history stack
    GU_DetailHandle bakedGeo;
    bakedGeo.allocateAndSet(new GU_Detail());
    const GU_Detail* srcGdp = it->second.gdp();
    if (srcGdp)
        bakedGeo.gdpNC()->copy(*srcGdp);

    myBakeHistory.push_back(bakedGeo);
    myBakeFrameHistory.push_back(myStartFrame);

    // Adopt the baked geometry as the new initial state at frame 1.
    myStartFrame = 1;
    myFrameCache.clear();
    myFrameCache[myStartFrame] = bakedGeo;
    myWindCache.clear();
    myWindCache[myStartFrame] = myWindSolver.getWindParticles();

    // Re-initialize internal simulation state from the baked geometry
    myGeo.initFromHoudiniGeo(bakedGeo.gdp());
    myGeo.initGrid();

    setInt("lock_frame", 0, t, 1);

    // Force a recook so the viewport updates
    forceRecook();
    return 1;
}

int SOP_Sanddial::performResetBake() {
    if (myBakeHistory.empty()) {
        // Nothing to undo — full reset
        myStartFrame = 1;
        myFrameCache.clear();
        forceRecook();
        return 1;
    }

    // Pop the most recent bake
    myStartFrame = myBakeFrameHistory.back();
    myBakeFrameHistory.pop_back();
    myBakeHistory.pop_back();

    // Invalidate the cache so we re-simulate from the restored start
    myFrameCache.clear();
    myWindCache.clear();
    forceRecook();
    return 1;
}

GU_DetailHandle SOP_Sanddial::getFrameResult(int frame, const GU_Detail* inputGeo, fpreal fps) {
    // Already cached?
    auto it = myFrameCache.find(frame);
    if (it != myFrameCache.end()) {
        const GU_Detail* cachedGdp = it->second.gdp();
        if (cachedGdp) {
            myGeo.initFromHoudiniGeo(cachedGdp);
            // Re-compute normals for the cached state to ensure mesh alignment
            myNormalsSolver.solve(myGeo);
            auto windIt = myWindCache.find(frame);
            if (windIt != myWindCache.end()) {
                myWindSolver.setWindParticles(windIt->second);
            }
        }
        return it->second;
    }

    fpreal dt = (1.0 / fps) * mySimTimestep;

    if (frame <= myStartFrame) {
        // Always use cached initial state if it exists (supports both baked
        // and painted erodibility that was written back to the start cache).
        auto startIt = myFrameCache.find(myStartFrame);
        if (startIt != myFrameCache.end() && startIt->second.gdp()) {
            myGeo.initFromHoudiniGeo(startIt->second.gdp());
            auto windIt = myWindCache.find(myStartFrame);
            if (windIt != myWindCache.end()) {
                myWindSolver.setWindParticles(windIt->second);
            }
            return startIt->second;
        }

        // Otherwise, initialise from upstream input geometry.
        initializeSimulation(inputGeo);
        GU_DetailHandle gdh;
        gdh.allocateAndSet(new GU_Detail());
        myGeo.writeToHoudiniGeo(gdh.gdpNC());
        myFrameCache[myStartFrame] = gdh;
        myWindCache[myStartFrame] = myWindSolver.getWindParticles();
        return gdh;
    }

    // Make sure previous frame is cached first.
    getFrameResult(frame - 1, inputGeo, fps);

    // Advance the simulation one step.
    advanceFrame(dt, frame);

    // Write result to a new handle.
    GU_DetailHandle gdh;
    gdh.allocateAndSet(new GU_Detail());
    myGeo.writeToHoudiniGeo(gdh.gdpNC());

    myFrameCache[frame] = gdh;
    myWindCache[frame] = myWindSolver.getWindParticles();
    return gdh;
}

// ── Brush application ───────────────────────────────────────────────────────
void SOP_Sanddial::applyBrushStroke(fpreal t, int frame) {
    UT_Vector3 brushCenter(
        evalFloat("brush_pos", 0, t),
        evalFloat("brush_pos", 1, t),
        evalFloat("brush_pos", 2, t));
    fpreal radius   = SYSmax(evalFloat("brush_radius",   0, t), fpreal(1e-5));
    fpreal strength = evalFloat("brush_strength", 0, t);
    fpreal falloff  = SYSmax(evalFloat("brush_falloff",  0, t), fpreal(0.01));
    int    bmode    = evalInt  ("brush_mode",     0, t);

    bool   surfaceOnly  = evalInt("brush_surface_only", 0, t) != 0;
    bool   depthEnabled = evalInt("brush_depth_enabled", 0, t) != 0;
    fpreal maxDepth     = evalFloat("brush_depth", 0, t);
    fpreal depthFalloff = evalFloat("brush_depth_falloff", 0, t);
    int    depthMode    = evalInt("brush_depth_mode", 0, t); // 0=Normal, 1=View

    UT_Vector3 refNormal(0, 1, 0);
    if (depthEnabled || !surfaceOnly) {
        if (depthMode == 0) { // Surface Normal alignment
            fpreal minD2 = 1e18;
            for (const auto& p : myGeo.particles) {
                if (!p.isSurface || p.isEroded) continue;
                fpreal d2 = (p.position - brushCenter).length2();
                if (d2 < minD2) {
                    minD2 = d2;
                    refNormal = p.normal;
                }
            }
        } else { // View alignment
            refNormal = -UT_Vector3(
                evalFloat("brush_view_dir", 0, t),
                evalFloat("brush_view_dir", 1, t),
                evalFloat("brush_view_dir", 2, t));
            refNormal.normalize();
        }
    }

    // Apply stroke directly to myGeo
    fpreal r2 = radius * radius;
    for (auto& p : myGeo.particles) {
        if (p.isEroded) continue;
        if (surfaceOnly && !p.isSurface) continue;

        UT_Vector3 diff = p.position - brushCenter;
        fpreal d2 = diff.dot(diff);
        if (d2 >= r2) continue;

        fpreal dist  = SYSsqrt(d2);
        fpreal alpha = fpreal(1.0) - SYSpow(dist / radius, falloff);

        // Depth-based filtering and attenuation
        if (depthEnabled) {
            // Depth d = projection of (brushCenter - p.pos) onto outward normal
            fpreal d = (brushCenter - p.position).dot(refNormal);
            if (d < -0.001 || d > maxDepth) continue; // Skip if too far "above" or too deep

            fpreal depthAlpha = fpreal(1.0) - SYSpow(SYSclamp(d / maxDepth, 0.0, 1.0), depthFalloff);
            alpha *= depthAlpha;
        }

        fpreal delta = alpha * strength;

        if      (bmode == 0) // Add
            p.erodibility = SYSclamp(p.erodibility + delta, fpreal(0.0), fpreal(1.0));
        else if (bmode == 1) // Subtract
            p.erodibility = SYSclamp(p.erodibility - delta, fpreal(0.0), fpreal(1.0));
        else                  // Overwrite
            p.erodibility = SYSclamp(delta, fpreal(0.0), fpreal(1.0));
    }

    // Write the painted state back into the CURRENT frame cache.
    GU_DetailHandle newResult;
    newResult.allocateAndSet(new GU_Detail());
    myGeo.writeToHoudiniGeo(newResult.gdpNC());
    myFrameCache[frame] = newResult;

    // Invalidate all FUTURE cached frames so they recook from this new state
    for (auto jt = myFrameCache.begin(); jt != myFrameCache.end(); ) {
        if (jt->first > frame) {
            myWindCache.erase(jt->first);
            jt = myFrameCache.erase(jt);
        } else {
            ++jt;
        }
    }
}

// ── Cook ────────────────────────────────────────────────────────────────────
OP_ERROR SOP_Sanddial::cookMySop(OP_Context& context) {
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
        return error();

    fpreal t = context.getTime();
    flags().setTimeDep(true);

    fpreal fps = OPgetDirector()->getChannelManager()->getSamplesPerSec();
    int frame = (int)SYSrint(t * fps) + 1;

    int simState = evalInt("sim_state", 0, t);
    if (simState == 0) {
        frame = evalInt("lock_frame", 0, t);
        if (frame < myStartFrame) frame = myStartFrame;
    }

    const GU_Detail* srcGeo = inputGeo(0, context);
    if (!srcGeo) {
        addError(SOP_MESSAGE, "No input geometry");
        return error();
    }

    GA_DataId currentDataId = srcGeo->getP()->getDataId();
    if (currentDataId != myInputDataId) {
        myFrameCache.clear();
        myWindCache.clear();
        myBakeHistory.clear();
        myBakeFrameHistory.clear();
        myStartFrame = 1;
        myInputDataId = currentDataId;
    }

    loadParameters(t);
    GU_DetailHandle result = getFrameResult(frame, srcGeo, fps);

    int brushActive = evalInt("brush_active", 0, t);
    if (brushActive != myLastBrushToggle) {
        myLastBrushToggle = brushActive;
        applyBrushStroke(t, frame);
        result = myFrameCache[frame];
    }

    // Output 0: Particles
    gdp->clearAndDestroy();
    const GU_Detail* resultGeo = result.gdp();
    if (resultGeo)
        gdp->copy(*resultGeo);

    // Viewport mode coloring
    int visualizeMode = evalInt("visualize_mode", 0, t);
    
    // If in Paint mode (1), we usually want to see erodibility unless "Nothing" is selected
    int viewportMode = evalInt("viewport_mode", 0, t);
    if (viewportMode == 1 && visualizeMode == 0) {
        visualizeMode = 1; // Default to Erodibility in Paint mode
    }

    if (visualizeMode > 0) {
        GA_RWHandleV3 cdH(gdp->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
        GA_ROHandleI sedH(gdp->findPointAttribute("isSediment"));

        if (cdH.isValid()) {
            if (visualizeMode == 1) { // Erodibility
                GA_ROHandleF erodH(gdp->findPointAttribute("erodibility"));
                if (erodH.isValid()) {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(gdp, ptoff) {
                        if (sedH.isValid() && sedH.get(ptoff)) {
                            cdH.set(ptoff, UT_Vector3(0.8, 0.7, 0.4)); // Sandy color
                            continue;
                        }
                        fpreal e = SYSclamp(erodH.get(ptoff), 0.0, 1.0);
                        UT_Vector3 color(e, 0.2 * (1.0 - e), 1.0 - e);
                        cdH.set(ptoff, color);
                    }
                }
            }
            else if (visualizeMode == 2) { // Viability
                GA_ROHandleF viabH(gdp->findPointAttribute("viability"));
                if (viabH.isValid()) {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(gdp, ptoff) {
                        if (sedH.isValid() && sedH.get(ptoff)) {
                            cdH.set(ptoff, UT_Vector3(0.8, 0.7, 0.4)); // Sandy color
                            continue;
                        }
                        fpreal v = SYSclamp(viabH.get(ptoff), 0.0, 1.0);
                        // Red (low viability) to Green (high viability)
                        UT_Vector3 color(1.0 - v, v, 0.2);
                        cdH.set(ptoff, color);
                    }
                }
            }
            else if (visualizeMode == 3) { // Stress
                GA_ROHandleF stressH(gdp->findPointAttribute("stress"));
                if (stressH.isValid()) {
                    fpreal maxStress = 0;
                    {
                        GA_Offset ptoff;
                        GA_FOR_ALL_PTOFF(gdp, ptoff) {
                            fpreal s = stressH.get(ptoff);
                            if (s > maxStress) maxStress = s;
                        }
                    }
                    if (maxStress < 1e-5) maxStress = 1.0;

                    {
                        GA_Offset ptoff;
                        GA_FOR_ALL_PTOFF(gdp, ptoff) {
                            if (sedH.isValid() && sedH.get(ptoff)) {
                                cdH.set(ptoff, UT_Vector3(0.8, 0.7, 0.4)); // Sandy color
                                continue;
                            }
                            fpreal s = stressH.get(ptoff) / maxStress;
                            s = SYSclamp(s, 0.0, 1.0);
                            // Heat map: Blue -> Cyan -> Green -> Yellow -> Red
                            UT_Vector3 color;
                            if (s < 0.25) color = UT_Vector3(0, s * 4, 1);
                            else if (s < 0.5) color = UT_Vector3(0, 1, 1 - (s - 0.25) * 4);
                            else if (s < 0.75) color = UT_Vector3((s - 0.5) * 4, 1, 0);
                            else color = UT_Vector3(1, 1 - (s - 0.75) * 4, 0);
                            cdH.set(ptoff, color);
                        }
                    }
                }
            }
            else if (visualizeMode == 4) { // Normals
                GA_ROHandleV3 normH(gdp->findPointAttribute("N"));
                if (normH.isValid()) {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(gdp, ptoff) {
                        UT_Vector3 n = normH.get(ptoff);
                        // Map [-1, 1] to [0, 1]
                        UT_Vector3 color = (n + UT_Vector3(1, 1, 1)) * 0.5;
                        cdH.set(ptoff, color);
                    }
                }
            }
            else if (visualizeMode == 5) { // Wind Deflation
                GA_ROHandleF valH(gdp->findPointAttribute("wind_deflation"));
                if (valH.isValid()) {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(gdp, ptoff) {
                        fpreal v = SYSclamp(valH.get(ptoff) * 100.0, 0.0, 1.0); // Boost for visibility
                        cdH.set(ptoff, UT_Vector3(v, v * 0.5, 0));
                    }
                }
            }
            else if (visualizeMode == 6) { // Wind Abrasion
                GA_ROHandleF valH(gdp->findPointAttribute("wind_abrasion"));
                if (valH.isValid()) {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(gdp, ptoff) {
                        fpreal v = SYSclamp(valH.get(ptoff) * 100.0, 0.0, 1.0);
                        cdH.set(ptoff, UT_Vector3(0, v, v));
                    }
                }
            }
            else if (visualizeMode == 7) { // Water
                GA_ROHandleF valH(gdp->findPointAttribute("water_erosion"));
                if (valH.isValid()) {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(gdp, ptoff) {
                        fpreal v = SYSclamp(valH.get(ptoff) * 100.0, 0.0, 1.0);
                        cdH.set(ptoff, UT_Vector3(0, 0, v));
                    }
                }
            }
            else if (visualizeMode == 8) { // Total Erosion
                GA_ROHandleF valH(gdp->findPointAttribute("total_erosion"));
                if (valH.isValid()) {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(gdp, ptoff) {
                        fpreal v = SYSclamp(valH.get(ptoff) * 100.0, 0.0, 1.0);
                        cdH.set(ptoff, UT_Vector3(v, v, v)); // Grayscale for total
                    }
                }
            }
        }
    } else {
        // If Nothing is selected, ensure Cd is removed if it was added by us
        gdp->destroyAttribute(GA_ATTRIB_POINT, "Cd");
    }

    return error();
}

OP_ERROR SOP_Sanddial::cookMyGuide1(OP_Context& context) {
    if (!myGuide1) return error();

    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
        return error();

    fpreal t = context.getTime();
    int showWind = evalInt("show_wind", 0, t);
    if (!showWind) {
        myGuide1->clearAndDestroy();
        return error();
    }

    myGuide1->clearAndDestroy();
    const auto& windParticles = myWindSolver.getWindParticles();

    if (windParticles.isEmpty()) return error();

    GA_RWHandleV3 cdH(myGuide1->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));

    for (const auto& wp : windParticles) {
        GA_Offset ptoff = myGuide1->appendPoint();
        myGuide1->setPos3(ptoff, wp.pos);
        if (cdH.isValid()) {
            // Light blue for wind particles
            cdH.set(ptoff, UT_Vector3(0.5, 0.7, 1.0));
        }
    }

    return error();
}

GU_DetailHandle SOP_Sanddial::cookMySopOutput(OP_Context& context, int outputidx, SOP_Node* interest) {
    if (outputidx == 0) {
        // This is usually handled by cookMySop, but we should return a valid handle if asked.
        cookMySop(context);
        GU_Detail* new_gdp = new GU_Detail();
        new_gdp->copy(*gdp);
        GU_DetailHandle handle;
        handle.allocateAndSet(new_gdp);
        return handle;
    }

    if (outputidx == 1 || outputidx == 2) {
        OP_AutoLockInputs inputs(this);
        if (inputs.lock(context) >= UT_ERROR_ABORT)
            return GU_DetailHandle();

        flags().setTimeDep(true);
        fpreal t = context.getTime();
        loadParameters(t);

        fpreal fps = OPgetDirector()->getChannelManager()->getSamplesPerSec();
        int frame = (int)SYSrint(t * fps) + 1;

        int simState = evalInt("sim_state", 0, t);
        if (simState == 0) {
            frame = evalInt("lock_frame", 0, t);
            if (frame < myStartFrame) frame = myStartFrame;
        }

        const GU_Detail* srcGeo = inputGeo(0, context);
        if (srcGeo) {
            // Ensure simulation is up to date for the mesh
            getFrameResult(frame, srcGeo, fps);
        }

        // Re-compute normals for meshing
        myNormalsSolver.solve(myGeo);

        MeshFilter filter = (outputidx == 1) ? MeshFilter::SandstoneOnly
                                              : MeshFilter::SedimentOnly;

        GU_Detail* meshGdp = new GU_Detail();
        myMesher.reconstruct(myGeo, meshGdp, myPoissonDepth,
                             (float)myPoissonScale, filter);

        if (mySubdivIterations > 0) {
            myLS3.subdivide(meshGdp, mySubdivIterations);
        }

        GU_DetailHandle handle;
        handle.allocateAndSet(meshGdp);
        return handle;
    }

    return GU_DetailHandle();
}
