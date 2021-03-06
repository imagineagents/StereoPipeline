// __BEGIN_LICENSE__
// Copyright (C) 2006-2011 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


#include <gtest/gtest.h>

#include <vw/Math/Vector.h>
#include <vw/Core/Debugging.h>
#include <asp/IsisIO/IsisCameraModel.h>
#include <vw/Cartography/SimplePointImageManipulation.h>
#include <test/Helpers.h>
#include <boost/foreach.hpp>

// Additional Headers required for ISIS
#include <Filename.h>
#include <CameraFactory.h>
#include <ProjectionFactory.h>
#include <Camera.h>
#include <Pvl.h>
#include <AlphaCube.h>
#include <CameraFocalPlaneMap.h>
#include <CameraDetectorMap.h>
#include <CameraDistortionMap.h>
#include <Projection.h>
#include <Latitude.h>
#include <Longitude.h>
#include <Distance.h>

using namespace vw;
using namespace vw::camera;

double DELTA = 1e-8;

Vector2 generate_random( int const& xsize,
                         int const& ysize ) {
  Vector2 pixel;
  pixel[0] = rand() % ( 10 * xsize - 10 ) + 10;
  pixel[0] /= 10.0;
  pixel[1] = rand() % ( 10 * ysize - 10 ) + 10;
  pixel[1] /= 10.0;
  return pixel;
}

TEST(IsisCameraModel, mapprojected) {
  std::string file("E0201461.tiny.cub");
  Isis::Filename cubefile( file.c_str() );
  Isis::Pvl label;
  label.Read( cubefile.Expanded() );
  Isis::Camera* cam = Isis::CameraFactory::Create( label );
  Isis::AlphaCube alphacube( label );
  Isis::Projection* proj = Isis::ProjectionFactory::CreateFromCube( label );

  // This is testing the assumption that we don't need to invoke much
  // of the camera model to do a pixel to vector.

  srand( 42 ); // We only want repeatible pseudo random
  for ( size_t i = 0; i < 2; i++ ) {
    Vector2 pixel = generate_random( cam->Samples(),
                                     cam->Lines() );
    Vector2 noise = generate_random( cam->Samples(),
                                     cam->Lines() );

    proj->SetWorld( pixel[0], pixel[1] );
    Vector3 lon_lat_radius( proj->UniversalLongitude(),
                            proj->UniversalLatitude(),
                            0 );
    if ( cam->HasElevationModel() ) {
      lon_lat_radius[2] =
        cam->DemRadius( Isis::Latitude(lon_lat_radius[1], Isis::Latitude::Degrees),
                        Isis::Longitude(lon_lat_radius[0], Isis::Longitude::Degrees) ).GetMeters();
    } else {
      vw_throw( NoImplErr() << " don't support ellipsoids at the moment" );
    }
    cam->SetImage( noise[0], noise[1] );
    cam->SetUniversalGround( lon_lat_radius[1],
                             lon_lat_radius[0],
                             lon_lat_radius[2] );
    EXPECT_VECTOR_NEAR( Vector2(cam->Sample(),cam->Line()), pixel, 1e-3 );
    double ip[3];
    cam->InstrumentPosition( ip );
    VectorProxy<double,3> instru( ip );
    double bc[3];
    cam->Coordinate( bc );
    VectorProxy<double,3> coord( bc );
    Vector3 alt = cartography::lon_lat_radius_to_xyz( lon_lat_radius );

    Vector3 dir = normalize( alt - instru );
    Vector3 new_point = instru*1000+50000*dir;
    new_point /= 1000;
  }

  delete cam;
  delete proj;
}

TEST(IsisCameraModel, groundmap_chk) {
  // Run two methods ..
  // solve for vector out with and without the groundmap solution
  // prove that they get the same values

  std::vector<std::string> files;
  files.push_back("E1701676.reduce.cub"); // Linescan
  files.push_back("5165r.cub");           // Frame
  files.push_back("5165r.map.cub");
  files.push_back("E0201461.tiny.cub");

  for ( size_t j = 0; j < files.size(); j++ ) {

    std::cout << "File: " << files[j] << "\n";
    std::cout << "------------------------------------\n";

    Isis::Filename cubefile( files[j].c_str() );
    Isis::Pvl label;
    label.Read( cubefile.Expanded() );
    Isis::Camera* cam = Isis::CameraFactory::Create( label );
    Isis::AlphaCube alphacube( label );

    std::cout << "CameraType: " << cam->GetCameraType() << "\n";

    double m_delta = 1e-8;
    if ( cam->HasProjection() )
      m_delta = 2e-2; // Cameras and Projections don't always line up

    // Ripping out the parts of the Camera Model
    Isis::CameraDistortionMap* distortmap = cam->DistortionMap();
    Isis::CameraFocalPlaneMap* focalmap = cam->FocalPlaneMap();
    Isis::CameraDetectorMap* detectmap = cam->DetectorMap();

    // Building test set
    std::vector<Vector2> pixel_sets;
    srand( 42 );
    for ( size_t i = 0; i < 1000; i++ ) {
      Vector2 pixel = generate_random( cam->Samples(),
                                       cam->Lines() );
      pixel_sets.push_back(pixel);
    }

    std::vector<Vector3> g_solution_sets, nog_solution_sets;

    Timer *t = new Timer("No GroundMap Solution");

    for ( size_t i = 0; i < pixel_sets.size(); i++ ) {
      Vector2 pixel = pixel_sets[i];
      Vector3 nog_solution;

      // No Ground Map Solution
      detectmap->SetParent( alphacube.AlphaSample(pixel[0]),
                            alphacube.AlphaLine(pixel[1]) );
      focalmap->SetDetector( detectmap->DetectorSample(),
                             detectmap->DetectorLine() );
      distortmap->SetFocalPlane( focalmap->FocalPlaneX(),
                                 focalmap->FocalPlaneY() );
      nog_solution[0] = distortmap->UndistortedFocalPlaneX();
      nog_solution[1] = distortmap->UndistortedFocalPlaneY();
      nog_solution[2] = distortmap->UndistortedFocalPlaneZ();
      nog_solution /= norm_2(nog_solution);
      std::vector<double> lookC(3); // Should make fancy func for std vec and vec
      std::copy( nog_solution.begin(), nog_solution.end(), lookC.begin() );
      std::vector<double> lookJ = cam->InstrumentRotation()->J2000Vector(lookC);
      lookC = cam->BodyRotation()->ReferenceVector(lookJ);
      std::copy( lookC.begin(), lookC.end(), nog_solution.begin() );

      nog_solution_sets.push_back(nog_solution);
    }

    delete(t);
    t = new Timer("Ground Map Solution:");

    for ( size_t i = 0; i < pixel_sets.size(); i++ ) {

      Vector2 pixel = pixel_sets[i];
      Vector3 g_solution;

      // Ground Map Solution
      cam->SetImage(pixel[0],pixel[1]);
      double p[3];
      cam->InstrumentPosition(p);
      Vector3 instrument( p[0], p[1], p[2] );

      cam->Coordinate(p);
      Vector3 ground( p[0], p[1], p[2] );
      g_solution = normalize( ground - instrument );

      g_solution_sets.push_back(g_solution);
    }

    delete t;

    for ( size_t i = 0; i < pixel_sets.size(); i++ )
      EXPECT_VECTOR_NEAR( nog_solution_sets[i],
                          g_solution_sets[i], m_delta );
  }
}

TEST(IsisCameraModel, camera_model) {
  // Circle Check
  std::vector<std::string> files;
  files.push_back("E1701676.reduce.cub");
  files.push_back("5165r.cub");
  files.push_back("E0201461.tiny.cub"); // Map Projected
  files.push_back("5165r.map.cub");

  srand( 42 );
  BOOST_FOREACH( std::string const& cube, files ) {
    IsisCameraModel cam(cube);

    std::cout << "File: " << cube << "\n";
    std::cout << "------------------------------------\n";

    Timer t(cube+"'s time: ");

    for ( size_t i = 0; i < 2; i++ ) {
      Vector2 pixel = generate_random( cam.samples(),
                                       cam.lines() );
      Vector3 point = cam.pixel_to_vector( pixel );
      for ( size_t k = 0; k < 2; k++ ) {
        // Apply noise to make sure we are not using stored values
        Vector2 noise = generate_random( cam.samples(),
                                         cam.lines() );
        Vector3 temp = cam.pixel_to_vector( noise );
      }
      point *= 70000; // 70 km below
      point += cam.camera_center( pixel );
      for ( size_t k = 0; k < 2; k++ ) {
        // Apply noise to make sure we are not using stored values
        Vector2 noise = generate_random( cam.samples(),
                                         cam.lines() );
        Vector3 temp = cam.camera_center( noise );
      }

      Vector2 rpixel = cam.point_to_pixel( point );
      EXPECT_VECTOR_NEAR( pixel, rpixel, 0.02 );
      EXPECT_TRUE( cam.ephemeris_time(Vector2()) );
      EXPECT_TRUE( cam.sun_position() != Vector3() );
    }

    Vector2 center_pixel( cam.lines(), cam.samples() );
    center_pixel /= 2;
    Quat center_pose = cam.camera_pose(center_pixel);
    double angle_from_z =
      acos(dot_prod(Vector3(0,0,1),inverse(center_pose).rotate(cam.pixel_to_vector(center_pixel))));
    EXPECT_LT( angle_from_z, 0.5 );
  }
}
