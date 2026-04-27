#pragma once

#include <SOP/SOP_Node.h>
#include <GU/GU_DetailHandle.h>
#include <GA/GA_Types.h>
#include <map>
#include <vector>
#include "AreniteGeometry.h"
#include "NormalsSolver.h"
#include "MpmSolver.h"
#include "WindSolver.h"
#include "WaterSolver.h"
#include "ErosionSolver.h"
#include "DepositionSolver.h"
#include "PoissonMesher.h"
#include "LS3Subdiv.h"

class SOP_Sanddial : public SOP_Node {
public:
    static OP_Node* myConstructor(OP_Network* net, const char* name, OP_Operator* op);
    static PRM_Template myTemplateList[];

    int performBake(fpreal t);
    int performResetBake();

protected:
    SOP_Sanddial(OP_Network* net, const char* name, OP_Operator* op);
    virtual ~SOP_Sanddial();

    virtual OP_ERROR cookMySop(OP_Context& context) override;
    virtual OP_ERROR cookMyGuide1(OP_Context& context) override;
    virtual GU_DetailHandle cookMySopOutput(OP_Context& context, int outputidx, SOP_Node* interest) override;

private:
    /// Initialize AreniteGeometry from the input Houdini geometry.
    void initializeSimulation(const GU_Detail* inputGeo);

    /// Advance one simulation step using the Arenite pipeline.
    void advanceFrame(fpreal dt, int frame);

    /// Read Parameter Pane values and configure solvers.
    void loadParameters(fpreal t);

    /// Ensure the cache contains the result for the given frame.
    GU_DetailHandle getFrameResult(int frame, const GU_Detail* inputGeo, fpreal fps);

    /// Apply a brush stroke to the current simulated state.
    /// Reads brush_pos, brush_radius, brush_strength, brush_falloff, brush_mode.
    void applyBrushStroke(fpreal t, int frame);

    // ── Simulation state ────────────────────────────────────────────────────
    AreniteGeometry  myGeo;
    NormalsSolver    myNormalsSolver;
    MpmSolver        myMpmSolver;
    WindSolver       myWindSolver;
    WaterSolver      myWaterSolver;
    ErosionSolver    myErosionSolver;
    DepositionSolver myDepositionSolver;
    PoissonMesher    myMesher;
    LS3Subdiv        myLS3;

    std::map<int, GU_DetailHandle> myFrameCache;
    std::vector<GU_DetailHandle> myBakeHistory;
    std::vector<int> myBakeFrameHistory;
    int myStartFrame = 1;
    int myLastBrushToggle = 0;
    GA_DataId myInputDataId = GA_INVALID_DATAID;
};

