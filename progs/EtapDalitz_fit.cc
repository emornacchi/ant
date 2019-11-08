#include "analysis/physics/etaprime/etaprime_dalitz.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#include "base/Logger.h"

#include "tclap/CmdLine.h"
#include "tclap/ValuesConstraintExtra.h"
#include "base/interval.h"
#include "base/WrapTFile.h"
#include "base/std_ext/system.h"
#include "base/ParticleType.h"

#include "analysis/plot/HistogramFactory.h"
#include "analysis/utils/ParticleTools.h"
#include "expconfig/ExpConfig.h"
#include "base/Detector_t.h"

#include "TH1D.h"
#include "TH2D.h"
#include "TCanvas.h"

#include "TSystem.h"
#include "TRint.h"
#include "TROOT.h"

#include "RooRealVar.h"
#include "RooConstVar.h"
#include "RooGaussian.h"
#include "RooArgusBG.h"
#include "RooAddPdf.h"
#include "RooDataSet.h"
#include "RooDataHist.h"
#include "RooPlot.h"
#include "RooHist.h"
#include "RooDataHist.h"
#include "RooAddition.h"
#include "RooProduct.h"
#include "RooFFTConvPdf.h"
#include "RooChi2Var.h"
#include "RooMinuit.h"
#include "RooFitResult.h"


using namespace ant;
using namespace std;
using namespace RooFit;


string concat_string(const vector<string>& strings, const string& delimiter = ", ")
{
    if (strings.empty())
        return "";

    return accumulate(next(strings.begin()), strings.end(), strings.front(),
            [&delimiter] (string& concat_str, const string& str) {
                return concat_str + delimiter + str;
            });
}

string cuts_path(const vector<string>& cuts, const char* delimiter = "/")
{
    if (cuts.empty())
        return "";

    stringstream s;
    copy(cuts.begin(), cuts.end(), ostream_iterator<string>(s, delimiter));
    return s.str();
}

string get_path(const string& cut_string, const string& tree)
{
    return tree + "/" + cut_string;
}


struct q2_bin_cut_t {
    string q2_bin;
    vector<string> cuts;
    string cut_string;

    void create_cut_string() {
        cut_string = cuts_path(cuts, "/");
    }

    q2_bin_cut_t(const string& bin, const string& cuts) : q2_bin(bin), cut_string(cuts) {}

    q2_bin_cut_t(const string& bin, const vector<string>& _cuts) : q2_bin(bin), cuts(_cuts) {
        create_cut_string();
    }
};


void test_path_building()
{
    const vector<string> cuts = {
        "selection",
        "KinFitProb > 0.1",
        "nothing",
        "thight cluster size"};
    const string tree = "EtapDalitz_plot_Sig";
    cout << "Test building tree path from cuts vector:" << endl
        << get_path(concat_string(cuts, "/"), tree) << endl;
    cout << "Test cuts path using copy and stringstream:" << endl
        << cuts_path(cuts) << endl;
}

void traverseCuts(TDirectory* dir, vector<vector<string>>& cuts) {
    auto keys = dir->GetListOfKeys();
    if (!keys)
        return;

    vector<string> dirnames;
    bool h_found = false;
    TIter nextk(keys);
    TKey* key;
    TKey* nextdir = nullptr;
    while ((key = static_cast<TKey*>(nextk())))
    {
        auto classPtr = TClass::GetClass(key->GetClassName());
        if (classPtr->InheritsFrom(TDirectory::Class())) {
            const string dirname(key->GetName());
            if (dirname == "h")
                h_found = true;
            else {
                nextdir = key;
                dirnames.emplace_back(dirname);
            }
        }
    }

    if (h_found && !dirnames.empty()) {
        cuts.emplace_back(dirnames);
        if (nextdir) {
            traverseCuts(dynamic_cast<TDirectory*>(nextdir->ReadObj()), cuts);
        }
    }
}

vector<vector<string>> extractCuts(const string& prefix, const WrapTFileInput& input) {
    TDirectory* prefixDir = nullptr;
    if (!input.GetObject(prefix, prefixDir))
        throw runtime_error("Cannot find prefix dir " + prefix);
    vector<vector<string>> cuts;
    traverseCuts(prefixDir, cuts);
    return cuts;
}

void print_extracted_cuts(const string& file)
{
    WrapTFileInput input(file);
    const string prefix = "EtapDalitz_plot_Sig";
    auto cuts = extractCuts(prefix, input);
    cout << "Extracted Cuts:" << endl;
    size_t cut_level = 0;
    for (const auto& vec : cuts) {
        cout << "  cut level " << ++cut_level << endl;
        for (const auto& cut : vec)
            cout << "    " << cut << endl;
    }
}



void reference_fit(const WrapTFileInput& input, const string& cuts)
{
    TH2D* ref_data;
    TH2D* ref_mc;
    TH1* h_data;
    TH1* h_mc;

    string hist = "EtapDalitz_plot_Ref/" + cuts +  "/h/Data/taggChannel_vs_etapIM_kinfitted";
    if (!input.GetObject(hist, ref_data))
        throw runtime_error("Couldn't find " + hist + " in file " + input.FileNames());

    hist = "EtapDalitz_plot_Ref/" + cuts +  "/h/Reference/taggChannel_vs_etapIM_kinfitted";
    if (!input.GetObject(hist, ref_mc))
        throw runtime_error("Couldn't find " + hist + " in file " + input.FileNames());

    auto EPT = ExpConfig::Setup::GetDetector<TaggerDetector_t>();

    const auto maxIM = [] (const double Eg) {
        const auto mp = ParticleTypeDatabase::Proton.Mass();
        return sqrt(mp*mp + 2*mp*Eg) - mp;
    };

    constexpr IntervalD fit_range = {840, 1020};

    constexpr int taggCh = 30;
    // loop here
    constexpr int taggBin = taggCh+1;
    h_data = ref_data->ProjectionX("h_data", taggBin, taggBin);
    if (taggCh == 40)  // close to threshold, decrease histogram IM range
        h_data->GetXaxis()->SetRangeUser(900,1100);
    h_mc = ref_mc->ProjectionX("h_mc", taggBin, taggBin);

    const double taggE = EPT->GetPhotonEnergy(taggCh);
    const double cutoff = maxIM(taggE);
    //cout << "EPT E = " << taggE << "; will use cutoff value: " << cutoff << endl;

    // define observable and ranges
    RooRealVar var_IM("IM","IM", fit_range.Start(), fit_range.Stop(), "MeV");
    var_IM.setBins(1000);
    var_IM.setRange("full", fit_range.Start(), fit_range.Stop());

    // load data to be fitted
    RooDataHist h_roo_data("h_roo_data","dataset",var_IM,h_data);

    // build shifted mc lineshape
    const double max_pos = h_data->GetBinCenter(h_data->GetMaximumBin()) - ParticleTypeDatabase::EtaPrime.Mass();
    RooRealVar var_IM_shift("var_IM_shift", "shift in IM", max_pos, -20., 20.);
    RooProduct var_IM_shift_invert("var_IM_shift_invert","shifted IM",RooArgSet(var_IM_shift, RooConst(-1.)));
    RooAddition var_IM_shifted("var_IM_shifted","shifted IM",RooArgSet(var_IM,var_IM_shift_invert));
    RooDataHist h_roo_mc("h_roo_mc","MC lineshape", var_IM, h_mc);
    RooHistPdf pdf_mc_lineshape("pdf_mc_lineshape","MC lineshape as PDF", var_IM_shifted, var_IM, h_roo_mc, 2);  // 2nd order interpolation (or 4th?)

    // build gaussian
    RooRealVar  var_gauss_sigma("gauss_sigma","detector resolution", 5., .01, 20.);
    RooGaussian pdf_gaussian("pdf_gaussian","Gaussian smearing", var_IM, RooConst(0.), var_gauss_sigma);

    // build signal as convolution, note that the gaussian must be the second PDF (see documentation)
    RooFFTConvPdf pdf_signal("pdf_signal","MC_lineshape (X) gauss",var_IM, pdf_mc_lineshape, pdf_gaussian) ;

    // build background with ARGUS function
    RooRealVar argus_cutoff("argus_cutoff","argus pos param", cutoff);  // upper threshold, calculated for beam energy
    RooRealVar argus_shape("argus_chi","argus shape param #chi", -5, -25., 5.);
    //RooRealVar argus_p("argus_p","argus p param", 0.5, 0, 1);
    RooRealVar argus_p("argus_p","argus p param", .5);
    RooArgusBG pdf_background("pdf_background","bkg argus",var_IM,argus_cutoff,argus_shape,argus_p);

    const double n_total = h_data->Integral();
    // build sum
    RooRealVar nsig("N_sig","#signal events", n_total/2, 0., 2*n_total);
    RooRealVar nbkg("N_bkg","#background events", n_total/2, 0., 2*n_total);
    RooAddPdf pdf_sum("pdf_sum","total sum",RooArgList(pdf_signal,pdf_background),RooArgList(nsig,nbkg));

    RooFitResult* fit = pdf_sum.fitTo(h_roo_data, Extended(), SumW2Error(kTRUE), Range("full"), Save(), PrintLevel(-1));
    RooPlot* frame = var_IM.frame();
    h_roo_data.plotOn(frame);
    frame->GetXaxis()->SetRangeUser(fit_range.Start(), fit_range.Stop());
    frame->SetTitle("Reference");

    auto p = new TPaveText();
    p->SetFillColor(0);
    p->SetFillStyle(0);
    p->SetX1NDC(0.14);
    p->SetX2NDC(0.39);
    p->SetY1NDC(0.38);
    p->SetY2NDC(0.86);
    p->SetTextSize(0.04f);

    // define lambda to insert lines in stat box
    const auto addLine = [] (TPaveText& p, const RooRealVar& v, const string& name = "") {
        p.InsertText(Form("%s = %.2f #pm %.2f", name.empty() ? v.GetName() : name.c_str(), v.getValV(), v.getError()));
    };

    pdf_sum.plotOn(frame, LineColor(kRed+1), PrintEvalErrors(-1));
    pdf_sum.plotOn(frame, Components(pdf_background), LineColor(kAzure-3), PrintEvalErrors(-1));
    pdf_sum.plotOn(frame, Components(pdf_signal), LineColor(kGreen+1));
    frame->Draw();
    pdf_sum.paramOn(frame);
    double chi2ndf = frame->chiSquare(fit->floatParsFinal().getSize());

    p->InsertText(Form("#chi^{2}/dof = %.2f", chi2ndf));
    addLine(*p, var_IM_shift,    "#Delta IM");
    addLine(*p, var_gauss_sigma, "#sigma");
    addLine(*p, argus_cutoff,    "c");
    addLine(*p, argus_shape,     "#chi");
    addLine(*p, argus_p,         "p");
    addLine(*p, nsig,            "n_{sig}");
    addLine(*p, nbkg,            "n_{bkg}");
    p->Draw();

    gPad->Modified();
    gPad->Update();
}


struct TCLAPInterval : interval<int> {
    using interval::interval;
    using ValueCategory = TCLAP::ValueLike;
};

int main(int argc, char** argv) {
    SetupLogger();

    // parse command line
    TCLAP::CmdLine cmd("EtapDalitz_fit", ' ', "0.1");
    auto cmd_verbose = cmd.add<TCLAP::ValueArg<int>>("v","verbose","Verbosity level (0..9)", false, 0,"int");
    auto cmd_batchmode = cmd.add<TCLAP::MultiSwitchArg>("b","batch","Run in batch mode (no ROOT shell afterwards)",false);
    auto cmd_debug = cmd.add<TCLAP::MultiSwitchArg>("","debug","Enable debug mode",false);

    auto cmd_ref = cmd.add<TCLAP::MultiSwitchArg>("r","reference","Run Reference Channel Analysis", false);
    auto cmd_ref_only = cmd.add<TCLAP::MultiSwitchArg>("","ref-only","Only Reference Channel Analysis", false);

    auto cmd_input = cmd.add<TCLAP::ValueArg<string>>("i","input","ROOT input file",true,"","rootfile");
    auto cmd_output = cmd.add<TCLAP::ValueArg<string>>("o","output","Output file",false,"","filename");
    TCLAP::ValuesConstraintExtra<decltype(ExpConfig::Setup::GetNames())> allowedsetupnames(ExpConfig::Setup::GetNames());
    auto cmd_setup  = cmd.add<TCLAP::ValueArg<string>>("s","setup","Choose setup by name",true,"", &allowedsetupnames);
    auto cmd_EPTrange = cmd.add<TCLAP::ValueArg<TCLAPInterval>>("c","EPTrange","EPT channel range for reference fits, e.g. 0-40",
                                                                false,TCLAPInterval{0,40},"channels");

    cmd.parse(argc, argv);

    const bool ref = cmd_ref->isSet();
    const bool ref_only = cmd_ref_only->isSet();

    // verbosity management
    if (cmd_verbose->isSet()) {
        el::Loggers::setVerboseLevel(cmd_verbose->getValue());
    }

    // do some tests in the beginning to make sure all functions work as expected
    if (cmd_debug->isSet()) {
        test_path_building();
        cout << "\nCall cut extraction method\n" << endl;
        print_extracted_cuts(cmd_input->getValue());
    }

    ExpConfig::Setup::SetByName(cmd_setup->getValue());

    WrapTFileInput input(cmd_input->getValue());

    const auto taggChRange = cmd_EPTrange->getValue();
    if (!taggChRange.IsSane()) {
        LOG(ERROR) << "Provided Tagger channel range " << taggChRange << " is not sane.";
        return EXIT_FAILURE;
    }
    if (cmd_EPTrange->isSet()) {
        LOG(WARNING) << "Using non-default Tagger channel range, may not yield correct results (debugging purposes)";
    }

    // create TRint as RooFit internally creates functions/histograms,
    // prevents this stupid gStyle=0 related error, sigh...
    argc=0; // prevent TRint to parse any cmdline
    TRint app("EtapDalitz_fit",&argc,argv,nullptr,0,true);
    if (cmd_batchmode->isSet())
        gROOT->SetBatch(true);

    unique_ptr<WrapTFileOutput> masterFile;
    if (cmd_output->isSet()) {
        // cd into masterFile upon creation
        masterFile = std_ext::make_unique<WrapTFileOutput>(cmd_output->getValue(), true);
    }


    if (ref || ref_only)
        reference_fit(input, "KinFitProb > 0.01/PID E cut < 0.3 MeV");


    // run TRint
    if (!cmd_batchmode->isSet()) {
        if (!std_ext::system::isInteractive()) {
            LOG(INFO) << "No TTY attached. Not starting ROOT shell.";
        }
        else {
            if (masterFile)
                LOG(INFO) << "Close ROOT properly to write data to disk.";

            app.Run(kTRUE); // really important to return...
            if (masterFile)
                LOG(INFO) << "Writing output file...";
            masterFile = nullptr;   // and to destroy the master WrapTFile before TRint is destroyed
        }
    }

    return EXIT_SUCCESS;
}