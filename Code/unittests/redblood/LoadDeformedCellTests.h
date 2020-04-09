#ifndef HEMELB_UNITTESTS_REDBLOOD_LOADDEFORMEDCELL_H
#define HEMELB_UNITTESTS_REDBLOOD_LOADDEFORMEDCELL_H

#include <cppunit/extensions/HelperMacros.h>
#include <boost/uuid/uuid_io.hpp>
#include <memory>

#include "SimulationMaster.h"
#include "lb/BuildSystemInterface.h"
#include "lb/lattices/D3Q19.h"
#include "Traits.h"
#include "redblood/Mesh.h"
#include "redblood/Cell.h"
#include "redblood/CellController.h"
#include "unittests/helpers/FolderTestFixture.h"

namespace hemelb
{
  namespace unittests
  {
    namespace redblood
    {
      class LoadDeformedCellTests : public hemelb::unittests::helpers::FolderTestFixture
      {
          CPPUNIT_TEST_SUITE (LoadDeformedCellTests);
          CPPUNIT_TEST (testIntegration);CPPUNIT_TEST_SUITE_END();

          typedef Traits<>::Reinstantiate<lb::lattices::D3Q19, lb::GuoForcingLBGK>::Type Traits;
          typedef hemelb::redblood::CellController<Traits> CellControl;
          typedef SimulationMaster<Traits> MasterSim;

        public:
          void setUp()
          {
            FolderTestFixture::setUp();
            CopyResourceToTempdir("large_cylinder_rbc.xml");
            CopyResourceToTempdir("large_cylinder.gmy");
            CopyResourceToTempdir("rbc_ico_720.vtp");
            CopyResourceToTempdir("992Particles_rank3_26_t992.vtp");

            // Run simulation for longer with no flow across the cylinder
            ModifyXMLInput("large_cylinder_rbc.xml", { "simulation", "steps", "value" }, 1000);
            ModifyXMLInput("large_cylinder_rbc.xml", { "inlets",
                                                       "inlet",
                                                       "condition",
                                                       "mean",
                                                       "value" },
                           0);

            // Remove cell insertion/removal and configuration
            DeleteXMLInput("large_cylinder_rbc.xml", { "inlets", "inlet", "insertcell" });
            DeleteXMLInput("large_cylinder_rbc.xml", { "inlets", "inlet", "flowextension" });
            DeleteXMLInput("large_cylinder_rbc.xml", { "outlets", "outlet", "flowextension" });
            DeleteXMLInput("large_cylinder_rbc.xml", { "redbloodcells", "cells" });

            argv[0] = "hemelb";
            argv[1] = "-in";
            argv[2] = "large_cylinder_rbc.xml";
            argv[3] = "-i";
            argv[4] = "0";
            argv[5] = "-ss";
            argv[6] = "1111";
            options = std::make_shared<hemelb::configuration::CommandLine>(argc, argv);

            master = std::make_shared<MasterSim>(*options, Comms());
          }

          void testIntegration()
          {
            // Read meshes from disc and correct coordinates
            auto const normal = readVTKMesh(resources::Resource("rbc_ico_720.vtp").Path().c_str());

            auto const deformed = readVTKMesh(resources::Resource("992Particles_rank3_26_t992.vtp").Path().c_str());
            // The file is stored in lattice units from a simulation with delta_x=2/3um and RBC radius 4um. We want to scale to a radius of 1.0 in lattice units
            double scaleInFile = 2e-6 / 3 * 1. / 4e-6;

            auto const barycent = barycenter(deformed->vertices);
            for (auto &vertex : deformed->vertices)
            {
              vertex = (vertex - barycent) * scaleInFile + barycent;
            }

            CPPUNIT_ASSERT(normal->facets == deformed->facets);
            CPPUNIT_ASSERT(volume(*normal) > 0.0);
            CPPUNIT_ASSERT(volume(deformed->vertices, normal->facets) > 0.0);
            std::cout << "VOLS: " << volume(*normal) << " " << volume(deformed->vertices, normal->facets) << std::endl;

            auto const & converter = master->GetUnitConverter();
            auto const scale = converter.ConvertToLatticeUnits("m", 4e-6);
            auto const cell = std::make_shared<redblood::Cell>(normal->vertices,
                                                               Mesh(normal),
                                                               scale);
            auto const sadcell = std::make_shared<redblood::Cell>(deformed->vertices,
                                                                  Mesh(normal),
                                                                  scale);

            // Scale and move to the centre of the domain
            *cell *= scale;
            *sadcell *= scale;
            *cell += converter.ConvertPositionToLatticeUnits(PhysicalPosition(0, 0, 0))
                - cell->GetBarycenter();
            *sadcell += converter.ConvertPositionToLatticeUnits(PhysicalPosition(0, 0, 0))
                - sadcell->GetBarycenter();

            writeVTKMesh("/tmp/ideal.vtp", cell, converter);
            writeVTKMesh("/tmp/deformed.vtp", sadcell, converter);

            sadcell->moduli.bending = 0.1;
            sadcell->moduli.strain = 0.1;
            sadcell->moduli.surface = 1e0;
            sadcell->moduli.volume = 1e0;
            sadcell->moduli.dilation = 0.75;

            auto controller = std::static_pointer_cast<CellControl>(master->GetCellController());
            controller->AddCell(sadcell);

            controller->AddCellChangeListener([&converter](const hemelb::redblood::CellContainer &cells)
            {
              static int iter = 0;
              for (auto cell : cells)
              {
                if(iter % 10 == 0)
                {
                  std::stringstream filename;
                  filename << cell->GetTag() << "_t_" << iter << ".vtp";
                  writeVTKMesh(filename.str(), cell, converter);
                }
              }
              ++iter;
            });

            // run the simulation
            master->RunSimulation();

            // Check simulation ran until the end
            AssertPresent("results/report.txt");
            AssertPresent("results/report.xml");

            // Recentre simulated cell
            writeVTKMesh("/tmp/reformed.vtp", sadcell, converter);
            *sadcell += converter.ConvertPositionToLatticeUnits(PhysicalPosition(0, 0, 0))
                - sadcell->GetBarycenter();
            writeVTKMesh("/tmp/reformed_centered.vtp", sadcell, converter);

//            // TODO: Align both cells for comparison
//            auto cell01 = cell->GetVertices()[350] - cell->GetVertices()[154];
//            auto sadcell01 = sadcell->GetVertices()[350] - sadcell->GetVertices()[154];
//            auto rotation = rotationMatrix(sadcell01, cell01);
//            *sadcell *= rotation;
//            writeVTKMesh("/tmp/reformed_centered_reorientied.vtp", sadcell, converter);
//            writeVTKMesh("/tmp/cell_end.vtp", cell, converter);
//
//            auto compareVertices = [] (util::Vector3D<double> const &left, util::Vector3D<double> const &right){
//              std::cout << left << std::endl;
//              std::cout << right << std::endl << std::endl;
//              return (left-right).GetMagnitudeSquared() < 1e-4;};
//            CPPUNIT_ASSERT(std::equal(cell->GetVertices().begin(), cell->GetVertices().end(), sadcell->GetVertices().begin(), compareVertices));
          }

        private:
          std::shared_ptr<MasterSim> master;
          std::shared_ptr<hemelb::configuration::CommandLine> options;
          int const argc = 7;
          char const * argv[7];

      };

      CPPUNIT_TEST_SUITE_REGISTRATION (LoadDeformedCellTests);
    } // namespace redblood
  } // namespace unittests
} // namespace hemelb

#endif