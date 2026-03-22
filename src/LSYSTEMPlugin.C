


#include <UT/UT_DSOVersion.h>
//#include <RE/RE_EGLServer.h>


#include <UT/UT_Math.h>
#include <UT/UT_Interrupt.h>
#include <GU/GU_Detail.h>
#include <GU/GU_PrimPoly.h>
#include <CH/CH_LocalVariable.h>
#include <PRM/PRM_Include.h>
#include <PRM/PRM_SpareData.h>
#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>


#include <limits.h>
#include "LSYSTEMPlugin.h"
using namespace HDK_Sample;

//
// Help is stored in a "wiki" style text file. 
//
// See the sample_install.sh file for an example.
//
// NOTE : Follow this tutorial if you have any problems setting up your visual studio 2008 for Houdini 
//  http://www.apileofgrains.nl/setting-up-the-hdk-for-houdini-12-with-visual-studio-2008/


///
/// newSopOperator is the hook that Houdini grabs from this dll
/// and invokes to register the SOP.  In this case we add ourselves
/// to the specified operator table.
///
void
newSopOperator(OP_OperatorTable *table)
{
    table->addOperator(
	    new OP_Operator("CusLsystem",			// Internal name
			    "MyLsystem",			// UI name
			     SOP_Lsystem::myConstructor,	// How to build the SOP
			     SOP_Lsystem::myTemplateList,	// My parameters
			     0,				// Min # of sources
			     0,				// Max # of sources
			     SOP_Lsystem::myVariables,	// Local variables
			     OP_FLAG_GENERATOR)		// Flag it as generator
	    );
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//PUT YOUR CODE HERE
//You need to declare your parameters here
//Example to declare a variable for angle you can do like this :
//static PRM_Name		angleName("angle", "Angle");

static PRM_Name angleName("angle", "Angle");
static PRM_Name angleScaleName("angle_scale", "Angle scale");
static PRM_Name angleNoiseName("angle_noise", "Angle noise");
static PRM_Name angleSeedName("angle_seed", "Angle noise seed");
static PRM_Name stepName("step", "Step size");
static PRM_Name stepScaleName("step_scale", "Step size scale");
static PRM_Name stepNoiseName("step_noise", "Step size noise");
static PRM_Name stepSeedName("step_seed", "Step size noise seed");
static PRM_Name gravityName("gravity", "Gravity");
static PRM_Name grammarName("grammar", "Grammar");
static PRM_Name nIterationsName("n_iterations", "# iterations");


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//				     ^^^^^^^^    ^^^^^^^^^^^^^^^
//				     internal    descriptive version


// PUT YOUR CODE HERE
// You need to setup the initial/default values for your parameters here
// For example : If you are declaring the inital value for the angle parameter
// static PRM_Default angleDefault(30.0);	

static PRM_Default angleDefault(30.0);
static PRM_Range angleRange(PRM_RANGE_UI, -180, PRM_RANGE_UI, 180);
static PRM_Default angleScaleDefault(1.0);
static PRM_Default angleNoiseDefault(0.0);
static PRM_Default angleSeedDefault(1);
static PRM_Default stepDefault(0.125);
static PRM_Default stepScaleDefault(1.0);
static PRM_Default stepNoiseDefault(0.0);
static PRM_Default stepSeedDefault(1);
static PRM_Default gravityDefault(0.0);
static PRM_Range gravityRange(PRM_RANGE_UI, 0, PRM_RANGE_UI, 100);
static PRM_Default grammarDefault(0, "F\nF->F[+F]F[-F]F");
static PRM_Default nIterationsDefault(4);


////////////////////////////////////////////////////////////////////////////////////////

PRM_Template
SOP_Lsystem::myTemplateList[] = {
// PUT YOUR CODE HERE
// You now need to fill this template with your parameter name and their default value
// EXAMPLE : For the angle parameter this is how you should add into the template
// PRM_Template(PRM_FLT,	PRM_Template::PRM_EXPORT_MIN, 1, &angleName, &angleDefault, 0),
// Similarly add all the other parameters in the template format here

    PRM_Template(PRM_FLT, PRM_Template::PRM_EXPORT_MIN, 1, &angleName, &angleDefault, 0, &angleRange),
    PRM_Template(PRM_FLT, PRM_Template::PRM_EXPORT_MIN, 1, &angleScaleName, &angleScaleDefault, 0),
    PRM_Template(PRM_FLT, PRM_Template::PRM_EXPORT_MIN, 1, &angleNoiseName, &angleNoiseDefault, 0),
    PRM_Template(PRM_INT, PRM_Template::PRM_EXPORT_MIN, 1, &angleSeedName, &angleSeedDefault, 0),
    PRM_Template(PRM_FLT, PRM_Template::PRM_EXPORT_MIN, 1, &stepName, &stepDefault, 0),
    PRM_Template(PRM_FLT, PRM_Template::PRM_EXPORT_MIN, 1, &stepScaleName, &stepScaleDefault, 0),
    PRM_Template(PRM_FLT, PRM_Template::PRM_EXPORT_MIN, 1, &stepNoiseName, &stepNoiseDefault, 0),
    PRM_Template(PRM_INT, PRM_Template::PRM_EXPORT_MIN, 1, &stepSeedName, &stepSeedDefault, 0),
    PRM_Template(PRM_FLT, PRM_Template::PRM_EXPORT_MIN, 1, &gravityName, &gravityDefault, 0, &gravityRange),
    PRM_Template(PRM_STRING, 1, &grammarName, &grammarDefault, 0),
    PRM_Template(PRM_INT, 1, &nIterationsName, &nIterationsDefault, 0),

/////////////////////////////////////////////////////////////////////////////////////////////

    PRM_Template()
};


// Here's how we define local variables for the SOP.
enum {
	VAR_PT,		// Point number of the star
	VAR_NPT		// Number of points in the star
};

CH_LocalVariable
SOP_Lsystem::myVariables[] = {
    { "PT",	VAR_PT, 0 },		// The table provides a mapping
    { "NPT",	VAR_NPT, 0 },		// from text string to integer token
    { 0, 0, 0 },
};

bool
SOP_Lsystem::evalVariableValue(fpreal &val, int index, int thread)
{
    // myCurrPoint will be negative when we're not cooking so only try to
    // handle the local variables when we have a valid myCurrPoint index.
    if (myCurrPoint >= 0)
    {
	// Note that "gdp" may be null here, so we do the safe thing
	// and cache values we are interested in.
	switch (index)
	{
	    case VAR_PT:
		val = (fpreal) myCurrPoint;
		return true;
	    case VAR_NPT:
		val = (fpreal) myTotalPoints;
		return true;
	    default:
		/* do nothing */;
	}
    }
    // Not one of our variables, must delegate to the base class.
    return SOP_Node::evalVariableValue(val, index, thread);
}

OP_Node *
SOP_Lsystem::myConstructor(OP_Network *net, const char *name, OP_Operator *op)
{
    return new SOP_Lsystem(net, name, op);
}

SOP_Lsystem::SOP_Lsystem(OP_Network *net, const char *name, OP_Operator *op)
	: SOP_Node(net, name, op)
{
    myCurrPoint = -1;	// To prevent garbage values from being returned
}

SOP_Lsystem::~SOP_Lsystem() {}

unsigned
SOP_Lsystem::disableParms()
{
    return 0;
}

OP_ERROR
SOP_Lsystem::cookMySop(OP_Context &context)
{
	fpreal		 now = context.getTime();

	// PUT YOUR CODE HERE
	// Decare the necessary variables and get always keep getting the current value in the node
	// For example to always get the current angle thats set in the node ,you need to :
	//    float angle;
	//    angle = ANGLE(now)       
    //    NOTE : ANGLE is a function that you need to use and it is declared in the header file to update your values instantly while cooking 
	LSystem myplant;

	float angle = ANGLE(now);
	float angleScale = ANGLE_SCALE(now);
	float angleNoise = ANGLE_NOISE(now);
	int angleSeed = ANGLE_SEED(now);
	float step = STEP(now);
	float stepScale = STEP_SCALE(now);
	float stepNoise = STEP_NOISE(now);
	int stepSeed = STEP_SEED(now);
	float gravity = GRAVITY(now);
	UT_String grammar;
	GRAMMAR(grammar, now);
	int nIterations = N_ITERATIONS(now);

	///////////////////////////////////////////////////////////////////////////

	//PUT YOUR CODE HERE
	// Next you need to call your Lystem cpp functions 
	// Below is an example , you need to call the same functions based on the variables you declare
    // myplant.loadProgramFromString("F\nF->F[+F]F[-F]";  
    // myplant.setDefaultAngle(30.0f);
    // myplant.setDefaultStep(1.0f);

	myplant.loadProgramFromString(grammar.toStdString());
	myplant.setDefaultAngle(angle);
	myplant.setDefaultAngleScale(angleScale);
	myplant.setAngleNoiseScale(angleNoise);
	myplant.setAngleNoiseSeed(angleSeed);
	myplant.setDefaultStep(step);
	myplant.setDefaultStepScale(stepScale);
	myplant.setStepNoiseScale(stepNoise);
	myplant.setStepNoiseSeed(stepSeed);
	myplant.setGravity(gravity);

	///////////////////////////////////////////////////////////////////////////////

	// PUT YOUR CODE HERE
	// You the need call the below function for all the genrations ,so that the end points points will be
	// stored in the branches vector , you need to declare them first

	//for (int i = 0; i < generations ; i++)
	//{
	//	  myplant.process(i, branches);
	//}

	std::vector<LSystem::Branch> branches;
	for (int i = 0; i < nIterations; i++)
	{
		myplant.process(i, branches);
	}

	///////////////////////////////////////////////////////////////////////////////////


	// Now that you have all the branches ,which is the start and end point of each point ,its time to render 
	// these branches into Houdini 
    

	// PUT YOUR CODE HERE
	// Declare all the necessary variables for drawing cylinders for each branch 
    float		 rad, tx, ty, tz;
    int			 divisions, plane;
    int			 xcoord =0, ycoord = 1, zcoord =2;
    float		 tmp;
    UT_Vector4		 pos;
    GU_PrimPoly		*poly;
    int			 i;
    UT_Interrupt	*boss;

    // Since we don't have inputs, we don't need to lock them.

    divisions  = 5;	// We need twice our divisions of points
    myTotalPoints = divisions;		// Set the NPT local variable value
    myCurrPoint   = 0;			// Initialize the PT local variable



    // Check to see that there hasn't been a critical error in cooking the SOP.
    if (error() < UT_ERROR_ABORT)
    {
	boss = UTgetInterrupt();
	if (divisions < 4)
	{
	    // With the range restriction we have on the divisions, this
	    //	is actually impossible, but it shows how to add an error
	    //	message or warning to the SOP.
	    addWarning(SOP_MESSAGE, "Invalid divisions");
	    divisions = 4;
	}
	gdp->clearAndDestroy();

	// Start the interrupt server
	if (boss->opStart("Building LSYSTEM"))
	{
        // PUT YOUR CODE HERE
	    // Build a polygon
	    // You need to build your cylinders inside Houdini from here
		// TIPS:
		// Use GU_PrimPoly poly = GU_PrimPoly::build(see what values it can take)
		// Also use GA_Offset ptoff = poly->getPointOffset()
		// and gdp->setPos3(ptoff,YOUR_POSITION_VECTOR) to build geometry.

		for (size_t j = 0; j < branches.size(); j++)
		{
			if (boss->opInterrupt()) break;

			vec3 start = branches[j].first;
			vec3 end = branches[j].second;


			poly = GU_PrimPoly::build(gdp, 2, GU_POLY_OPEN);

			GA_Offset pointOffset0 = poly->getPointOffset(0);
			GA_Offset pointOffset1 = poly->getPointOffset(1);

			gdp->setPos3(pointOffset0, UT_Vector3(start[0], start[1], start[2]));
			gdp->setPos3(pointOffset1, UT_Vector3(end[0], end[1], end[2]));
		}

		////////////////////////////////////////////////////////////////////////////////////////////


























		////////////////////////////////////////////////////////////////////////////////////////////

	    // Highlight the star which we have just generated.  This routine
	    // call clears any currently highlighted geometry, and then it
	    // highlights every primitive for this SOP. 
	    select(GU_SPrimitive);
	}

	// Tell the interrupt server that we've completed. Must do this
	// regardless of what opStart() returns.
	boss->opEnd();
    }

    myCurrPoint = -1;
    return error();
}

