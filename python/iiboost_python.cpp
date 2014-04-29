#include <stdexcept>
#include <cstdio>
#include <cstring>

// error handling (name comes from Qt)
#define qFatal(...) do { char errStr[8192]; sprintf(errStr, "ERROR: "); sprintf(errStr, __VA_ARGS__);sprintf(errStr, "\n"); \
 					     throw std::runtime_error(errStr); } while(0)
#define qDebug(...) do { fprintf (stdout, __VA_ARGS__); fprintf(stdout, "\n"); fflush(stdout); } while(0)


#include "ROIData.h"
#include "BoosterInputData.h"
#include "ContextFeatures/ContextRelativePoses.h"

#include "Booster.h"
#include "utils/TimerRT.h"

#include <Python.h>

typedef float PredictionPixelType;

extern "C"
{
	void freeModel( void *modelPtr )
	{
		if (modelPtr == 0)	return;

		delete ((BoosterModel *)modelPtr);
	}

	// assumes that predPtr is already allocated, of same size as imgPtr
	void predict( void *modelPtr, ImagePixelType *imgPtr, int width, int height, int depth, PredictionPixelType *predPtr )
	{
		Matrix3D<PredictionPixelType> predMatrix;
		predMatrix.fromSharedData( predPtr, width, height, depth );

		// create roi for image, no GT available
		ROIData roi;
		roi.init( imgPtr, 0, 0, 0, width, height, depth );

		// raw image to integral image
		ROIData::IntegralImageType ii;
		ii.compute( roi.rawImage );
		roi.addII( ii.internalImage().data() );


		MultipleROIData allROIs;
		allROIs.add( &roi );

		Booster adaboost;
		adaboost.setModel( *((BoosterModel *) modelPtr) );

		adaboost.predict( &allROIs, &predMatrix );
	}

	// input: multiple imgPtr, gtPtr (arrays of pointers)
	//		  multiple img sizes
	// returns a BoosterModel *
	// -- BEWARE: this function is a mix of dirty tricks right now
	void * train( ImagePixelType **imgPtr, GTPixelType **gtPtr, 
				  int *width, int *height, int *depth,
				  int numStacks,
				  int numStumps, int debugOutput )
	{
		BoosterModel *modelPtr = 0;

		try
		{
			ROIData rois[numStacks];					// TODO: not C++99 compatible?
			ROIData::IntegralImageType ii[numStacks];	// TODO: remove
			MultipleROIData allROIs;

			for (int i=0; i < numStacks; i++)
			{
				rois[i].init( imgPtr[i], gtPtr[i], 0, 0, width[i], height[i], depth[i] );

				// raw image to integral image
				// TODO: this should be removed and passed directly to train()
				ii[i].compute( rois[i].rawImage );
				rois[i].addII( ii[i].internalImage().data() );

				allROIs.add( &rois[i] );
			}

			// debug
			// rois[0].rawImage.save("/tmp/test.nrrd");

			BoosterInputData bdata;
			bdata.init( &allROIs );
			bdata.showInfo();

			Booster adaboost;
			adaboost.setShowDebugInfo( debugOutput != 0 );

			adaboost.train( bdata, numStumps );

			// create by copying
			modelPtr = new BoosterModel( adaboost.model() );
		}
		catch(std::exception const& e)
		{
			PyErr_SetString(PyExc_RuntimeError, e.what());
		}

		return modelPtr;
	}
}