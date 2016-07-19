#include "base/Logger.h"

#include "analysis/plot/CutTree.h"
#include "analysis/physics/etaprime/etaprime_ept.h"

#include "expconfig/ExpConfig.h"
#include "expconfig/detectors/EPT.h"

#include "base/CmdLine.h"
#include "base/interval.h"
#include "base/printable.h"
#include "base/WrapTFile.h"
#include "base/std_ext/string.h"
#include "base/std_ext/system.h"
#include "base/ProgressCounter.h"

#include "TSystem.h"
#include "TRint.h"

using namespace ant;
using namespace ant::analysis;
using namespace ant::analysis::plot;
using namespace std;

volatile bool interrupt = false;

// define the structs containing the histograms
// and the cuts. for simple branch variables, that could
// be combined...

struct Hist_t {
    using Fill_t = physics::EtapEPT::Tree_t;

    std::vector<TH2D*> h_IM_2g;

    Hist_t(HistogramFactory HistFac, cuttree::TreeInfo_t)
    {
        HistogramFactory h("h", HistFac);
        auto ept = ExpConfig::Setup::GetDetector<expconfig::detector::EPT>();
        for(unsigned ch=0;ch<ept->GetNChannels();ch++) {
            h_IM_2g.push_back(
                        h.makeTH2D(std_ext::formatter() << "IM 2g Ch=" << ch,
                                   "IM / MeV","",
                                   BinSettings(100,800,1050),
                                   BinSettings(ept->GetNChannels()),
                                   "h_IM_2g_Ch"+to_string(ch))
                        );
        }
    }


    void Fill(const Fill_t& f) const
    {
        h_IM_2g[f.TaggCh]->Fill(f.IM_2g, f.TaggCh_, f.TaggW);
    }

    // Sig and Ref channel (can) share some cuts...
    static cuttree::Cuts_t<Fill_t> GetCuts() {
        using cuttree::MultiCut_t;
        cuttree::Cuts_t<Fill_t> cuts;
//        cuts.emplace_back(MultiCut_t<Fill_t>{
//                              // Use non-null PID cuts only when PID calibrated...
//                              {"CBSumVeto=0", [] (const Fill_t& f) { return f.Shared.CBSumVetoE==0; } },
////                              {"CBSumVeto<0.25", [] (const Fill_t& f) { return f.Shared.CBSumVetoE<0.25; } },
//                              {"PIDSumE=0", [] (const Fill_t& f) { return f.Common.PIDSumE==0; } },
////                              {"PIDSumE<1", [] (const Fill_t& f) { return f.Common.PIDSumE<1; } },
//                          });
        cuts.emplace_back(MultiCut_t<Fill_t>{
                              {"DiscardedEk=0", [] (const Fill_t& f) { return f.DiscardedEk == 0; } },
                              {"DiscardedEk<50", [] (const Fill_t& f) { return f.DiscardedEk < 50; } },
//                              {"DiscardedEk<100", [] (const Fill_t& f) { return f.Shared.DiscardedEk < 100; } },
                          });
        cuts.emplace_back(MultiCut_t<Fill_t>{
                              {"KinFitProb>0.01", [] (const Fill_t& f) { return f.KinFitProb>0.01; } },
//                                 {"KinFitProb>0.1", [] (const Fill_t& f) { return f.Shared.KinFitProb>0.1; } },
//                                 {"KinFitProb>0.3", [] (const Fill_t& f) { return f.Shared.KinFitProb>0.3; } },
                          });
        return cuts;
    }
};

int main(int argc, char** argv) {
    SetupLogger();

    signal(SIGINT, [] (int) { interrupt = true; } );

    TCLAP::CmdLine cmd("plot", ' ', "0.1");
    auto cmd_input = cmd.add<TCLAP::ValueArg<string>>("i","input","Input file",true,"","input");
    auto cmd_batchmode = cmd.add<TCLAP::MultiSwitchArg>("b","batch","Run in batch mode (no ROOT shell afterwards)",false);
    auto cmd_maxevents = cmd.add<TCLAP::MultiArg<int>>("m","maxevents","Process only max events",false,"maxevents");
    auto cmd_output = cmd.add<TCLAP::ValueArg<string>>("o","output","Output file",false,"","filename");

    auto cmd_setupname = cmd.add<TCLAP::ValueArg<string>>("s","setup","Override setup name", false, "Setup_2014_07_EPT_Prod", "setup");

    cmd.parse(argc, argv);

    const auto setup_name = cmd_setupname->getValue();
    auto setup = ExpConfig::Setup::Get(setup_name);
    if(setup == nullptr) {
        LOG(ERROR) << "Did not find setup instance for name " << setup_name;
        return 1;
    }



    WrapTFileInput input(cmd_input->getValue());

    physics::EtapEPT::Tree_t tree;
    {
        TTree* t;
        if(!input.GetObject("EtapEPT/tree",t)) {
            LOG(ERROR) << "Cannot find EtapEPT/tree in input";
            return 1;
        }
        if(!tree.Matches(t)) {
            LOG(ERROR) << "Found tree does not match";
            return 1;
        }
        tree.LinkBranches(t);
    }


    auto entries = tree.Tree->GetEntries();

    unique_ptr<WrapTFileOutput> masterFile;
    if(cmd_output->isSet()) {
        masterFile = std_ext::make_unique<WrapTFileOutput>(cmd_output->getValue(),
                                                    WrapTFileOutput::mode_t::recreate,
                                                     true); // cd into masterFile upon creation
    }

    HistogramFactory HistFac("EtapEPT");

    auto cuttree = cuttree::Make<Hist_t>(HistFac, "EtapEPT", Hist_t::GetCuts());


    LOG(INFO) << "Tree entries=" << entries;
    auto max_entries = entries;
    if(cmd_maxevents->isSet() && cmd_maxevents->getValue().back()<entries) {
        max_entries = cmd_maxevents->getValue().back();
        LOG(INFO) << "Running until " << max_entries;
    }

    long long entry = 0;
    ProgressCounter::Interval = 3;
    ProgressCounter progress(
                [&entry, entries] (std::chrono::duration<double>) {
        LOG(INFO) << "Processed " << 100.0*entry/entries << " %";
    });

    for(entry=0;entry<max_entries;entry++) {
        if(interrupt)
            break;

        tree.Tree->GetEntry(entry);

        cuttree::Fill<Hist_t>(cuttree, tree);

        ProgressCounter::Tick();
    }

    if(!cmd_batchmode->isSet()) {
        if(!std_ext::system::isInteractive()) {
            LOG(INFO) << "No TTY attached. Not starting ROOT shell.";
        }
        else {

            argc=0; // prevent TRint to parse any cmdline
            TRint app("EtapOmegaG_plot",&argc,argv,nullptr,0,true);

            if(masterFile)
                LOG(INFO) << "Stopped running, but close ROOT properly to write data to disk.";

            app.Run(kTRUE); // really important to return...
            if(masterFile)
                LOG(INFO) << "Writing output file...";
            masterFile = nullptr;   // and to destroy the master WrapTFile before TRint is destroyed
        }
    }


    return 0;
}
