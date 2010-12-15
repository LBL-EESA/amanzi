// -------------------------------------------------------------
/**
 * @file   test_Read.cc
 * @author William A. Perkins
 * @date Mon Dec 13 10:30:27 2010
 * 
 * @brief Some unit tests for reading a (serial) Exodus file and
 * building a STK_mesh::Mesh_maps_stk instance.
 * 
 * 
 */

// -------------------------------------------------------------
// -------------------------------------------------------------
// Created November 22, 2010 by William A. Perkins
// Last Change: Mon Dec 13 10:30:27 2010 by William A. Perkins <d3g096@PE10900.pnl.gov>
// -------------------------------------------------------------

#include <sstream>
#include <UnitTest++.h>
#include <Epetra_MpiComm.h>

#include "Exodus_readers.hh"
#include "Parallel_Exodus_file.hh"
#include "../Mesh_factory.hh"
#include "Auditor.hh"

// -------------------------------------------------------------
// class ReadFixture
// -------------------------------------------------------------
class ReadFixture {
protected: 

  static const bool verbose;
  virtual void my_read(const std::string& name) = 0;

public:

  Epetra_MpiComm comm;
  const int nproc;

  STK_mesh::Mesh_factory mf;
  Mesh_data::Fields nofields;
  Teuchos::RCP<Mesh_data::Data> meshdata;
  STK_mesh::Mesh_p mesh;
  Teuchos::RCP<STK_mesh::Mesh_maps_stk> maps;
  
  /// Default constructor.
  ReadFixture(void)
    : comm(MPI_COMM_WORLD),
      nproc(comm.NumProc()),
      mf(comm.Comm(), 1000)
  { }

  void read(const std::string& name)
  {
    int ierr(0);
    try { 
      my_read(name);
      // maps.reset(new STK_mesh::Mesh_maps_stk(mesh));
    } catch (const std::exception& e) {
      std::cerr << comm.MyPID() << ": error: " << e.what() << std::endl;
      ierr++;
    }
    int aerr(0);
    comm.SumAll(&ierr, &aerr, 1);

    if (aerr) {
      throw std::runtime_error("Problem in ReadFixture");
    }
  }
  
};

const bool ReadFixture::verbose(false);


// -------------------------------------------------------------
//  class SerialReadFixture
// -------------------------------------------------------------
class SerialReadFixture : public ReadFixture {
protected:
  
    void my_read(const std::string& name)         
    {
        meshdata.reset(ExodusII::read_exodus_file(name.c_str()));
        // meshdata->to_stream(std::cerr, verbose);
        mesh.reset(mf.build_mesh(*meshdata, nofields));
    }

public:

  /// Default constructor.
  SerialReadFixture(void) 
    : ReadFixture()
  {}

};

// -------------------------------------------------------------
//  class ParallelReadFixture
// -------------------------------------------------------------
class ParallelReadFixture : public ReadFixture {
protected:
  
  void my_read(const std::string& name) 
  {
      ExodusII::Parallel_Exodus_file thefile(comm, name);
      meshdata = thefile.read_mesh();

      for (int p = 0; p < nproc; p++) {
        if (comm.MyPID() == p) {
          std::cerr << std::endl;
          std::cerr << ">>>>>> Process " << p << " Begin <<<<<<" << std::endl;
          meshdata->to_stream(std::cerr, verbose);
          std::cerr << ">>>>>> Process " << p << " End <<<<<<" << std::endl;
          std::cerr << std::endl;
        }
        comm.Barrier();
      }

      Teuchos::RCP<Epetra_Map> cmap(thefile.cellmap());
      Teuchos::RCP<Epetra_Map> vmap(thefile.vertexmap());
      if (verbose) {
        cmap->Print(std::cerr);
        vmap->Print(std::cerr);
      }
      mesh.reset(mf.build_mesh(*meshdata, *cmap, *vmap, nofields));
  }

public:

  /// Default constructor.
  ParallelReadFixture(void) 
    : ReadFixture()
  {}

};

  

SUITE (Exodus)
{
    TEST (Processes) 
    {
        Epetra_MpiComm comm(MPI_COMM_WORLD);
        const int nproc(comm.NumProc());
        CHECK(nproc >= 1 && nproc <= 4);
    }

    TEST_FIXTURE (SerialReadFixture, SerialReader1)
    {
        if (nproc == 1) {
            read("../exodus/test_files/hex_11x11x11_ss.exo");
            CHECK_EQUAL(mesh->count_entities(stk::mesh::Node, OWNED), 11*11*11);
            CHECK_EQUAL(mesh->count_entities(stk::mesh::Face, OWNED), 10*10*11*3);
            CHECK_EQUAL(mesh->count_entities(stk::mesh::Element, OWNED), 10*10*10);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Element), 3);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Node), 20);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Face), 20);

            stk::mesh::Part *p;
            int count;

            p = mesh->get_set(1, stk::mesh::Face);
            CHECK(p != NULL);
            count = mesh->count_entities(*p, OWNED);
            CHECK_EQUAL(count, 600);
            
            p = mesh->get_set("element block 20000", stk::mesh::Element);
            CHECK(p != NULL);
            count = mesh->count_entities(*p, OWNED);
            CHECK_EQUAL(count, 9);
            
            p = mesh->get_set("node set 103", stk::mesh::Node);
            CHECK(p != NULL);
            count = mesh->count_entities(*p, OWNED);
            CHECK_EQUAL(count, 121);
            
            Auditor audit("hex_11x11x11_ss_", mesh);
            audit();
        }
    }

    TEST_FIXTURE (SerialReadFixture, SerialReader2)
    {
        if (nproc == 1) {
            read("../exodus/test_files/hex_4x4x4_ss.exo");
            CHECK_EQUAL(mesh->count_entities(stk::mesh::Node, OWNED), 4*4*4);
            CHECK_EQUAL(mesh->count_entities(stk::mesh::Face, OWNED), 3*3*4*3);
            CHECK_EQUAL(mesh->count_entities(stk::mesh::Element, OWNED), 3*3*3);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Element), 3);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Node), 21);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Face), 21);

            stk::mesh::Part *p;
            int count;

            p = mesh->get_set("side set 1", stk::mesh::Face);
            CHECK(p != NULL);
            count = mesh->count_entities(*p, OWNED);
            CHECK_EQUAL(count, 54);
            
            p = mesh->get_set("element block 20000", stk::mesh::Element);
            CHECK(p != NULL);
            count = mesh->count_entities(*p, OWNED);
            CHECK_EQUAL(count, 9);
            
            p = mesh->get_set("node set 103", stk::mesh::Node);
            CHECK(p != NULL);
            count = mesh->count_entities(*p, OWNED);
            CHECK_EQUAL(count, 16);
            
            Auditor audit("hex_4x4x4_ss_", mesh);
            audit();
       }
    }

    TEST_FIXTURE (ParallelReadFixture, ParallelReader1)
    {
        if (nproc > 1 && nproc <= 4) {
            read("../exodus/test_files/split1/hex_11x11x11_ss.par");
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Element), 3);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Node), 20);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Face), 20);
            int local, global;
            local = mesh->count_entities(stk::mesh::Node, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 11*11*11);
            local = mesh->count_entities(stk::mesh::Face, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 10*10*11*3);
            local = mesh->count_entities(stk::mesh::Element, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 10*10*10);

            stk::mesh::Part *p;

            p = mesh->get_set("side set 1", stk::mesh::Face);
            CHECK(p != NULL);
            local = mesh->count_entities(*p, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 600);
            
            p = mesh->get_set("element block 20000", stk::mesh::Element);
            CHECK(p != NULL);
            local = mesh->count_entities(*p, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 9);

            p = mesh->get_set("node set 103", stk::mesh::Node);
            CHECK(p != NULL);
            local = mesh->count_entities(*p, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 121);

            Auditor audit("hex_11x11x11_ss.par", mesh);
            audit();
        }
    }            

    TEST_FIXTURE (ParallelReadFixture, ParallelReader2)
    {
        if (nproc > 1 && nproc < 4) {
            read("../exodus/test_files/split1/hex_4x4x4_ss.par");
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Element), 3);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Node), 21);
            CHECK_EQUAL(mesh->num_sets(stk::mesh::Face), 21);
            int local, global;
            local = mesh->count_entities(stk::mesh::Node, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 4*4*4);
            local = mesh->count_entities(stk::mesh::Face, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 3*3*4*3);
            local = mesh->count_entities(stk::mesh::Element, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 3*3*3);

            stk::mesh::Part *p;

            p = mesh->get_set(1, stk::mesh::Face);
            CHECK(p != NULL);
            local = mesh->count_entities(*p, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 54);
            
            p = mesh->get_set(20000, stk::mesh::Element);
            CHECK(p != NULL);
            local = mesh->count_entities(*p, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 9);

            p = mesh->get_set(103, stk::mesh::Node);
            CHECK(p != NULL);
            local = mesh->count_entities(*p, OWNED);
            comm.SumAll(&local, &global, 1);
            CHECK_EQUAL(global, 16);

            Auditor audit("hex_4x4x4_ss.par", mesh);
            audit();
        }
    }            


    TEST (DirectReader) 
    {
        Epetra_MpiComm comm(MPI_COMM_WORLD);

        // FIXME: need to be able to assign path from configuration

        std::string fpath("../exodus/test_files/");
        std::string fname("hex_11x11x11_ss");
        if (comm.NumProc() == 1) {
            fname += ".exo";
        } else {
            fpath += "split1/";
            fname += ".par";
        }
        fname = fpath + fname;

        Teuchos::RCP<Mesh_maps_base> 
            mesh_map(new STK_mesh::Mesh_maps_stk(comm, fname.c_str()));

        Auditor audit("stk_mesh_read_", mesh_map);
        audit();
    }
      
} 

