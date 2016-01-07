#include "ExtractResolutions.h"

#include "TTree.h"

using namespace ant;
using namespace ant::std_ext;
using namespace ant::analysis;
using namespace ant::analysis::physics;
using namespace std;

physics::ExtractResolutions::ExtractResolutions(const std::string& name, PhysOptPtr opts):
    Physics(name, opts)
{
    if(opts->Get<string>("Detector") == "CB") {
        det = Detector_t::Type_t::CB;
        HistFac.SetTitlePrefix("CB");
    } else if(opts->Get<string>("Detector") == "TAPS") {
        det = Detector_t::Type_t::TAPS;
        HistFac.SetTitlePrefix("TAPS");
    }

    tree = HistFac.makeTTree("tree");
    tree->Branch("E",       &b_DE);
    tree->Branch("Theta",   &b_DTheta);
    tree->Branch("Phi",     &b_DPhi);
    tree->Branch("Element", &b_Element);
    tree->Branch("tE",      &b_E);
}

void physics::ExtractResolutions::ProcessEvent(const data::Event& event)
{
    const auto& mcparticles = event.MCTrue.Particles.GetAll();

    if(mcparticles.size() == 1) {

        const auto& recparticles = event.Reconstructed.Particles.GetAll();

        if(recparticles.size() ==1 ) {

            const auto& mcp = mcparticles.front();
            const auto& rep = recparticles.front();

            if(rep->Candidate && rep->Candidate->GetDetector() == det) {
                const auto& c = rep->Candidate->FindCaloCluster();
                if(c) {

                    b_DE      = rep->Ek() - mcp->Ek();
                    b_DTheta  = rep->Theta() - mcp->Theta();
                    b_DPhi    = TVector2::Phi_mpi_pi(rep->Phi() - mcp->Phi());
                    b_Element = c->CentralElement;
                    b_E       = mcp->Ek();

                    tree->Fill();
                }
            }
        }
    }
}

void physics::ExtractResolutions::ShowResult()
{

}

AUTO_REGISTER_PHYSICS(ExtractResolutions)
