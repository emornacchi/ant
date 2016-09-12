#pragma once

#include "base/std_ext/misc.h"
#include "utils/combinatorics.h"
#include "analysis/physics/Physics.h"
#include "plot/PromptRandomHist.h"
#include "analysis/utils/Fitter.h"
#include "TLorentzVector.h"

class TH1D;

namespace ant {
namespace analysis {
namespace physics {

class IMCombFitPlots : public Physics {
protected:
    const unsigned MAX_GAMMA;
    static constexpr bool PROBABILITY_CUT = false;
    static constexpr double PROBABILITY = .01;

public:
    PromptRandom::Switch prs;
    std::vector<PromptRandom::Hist1> raw_2;
    std::vector<PromptRandom::Hist1> raw_n;
    std::vector<PromptRandom::Hist1> fit_2;
    std::vector<PromptRandom::Hist1> fit_n;

    utils::UncertaintyModelPtr model;
    std::vector<utils::KinFitter> kinfit;

    unsigned MinNGamma() const noexcept { return 2; }
    unsigned MaxNGamma() const noexcept { return MAX_GAMMA; }


public:
    IMCombFitPlots(const std::string& name, OptionsPtr opts);

    template<typename T>
    bool shift_right(std::vector<T>&);
    template <typename iter>
    LorentzVec sumlv(iter start, iter end);

    static APLCON::Fit_Settings_t MakeFitSettings(unsigned);

    bool find_best_comb(const TTaggerHit&, TCandidatePtrList&, TParticleList&);

    virtual void ProcessEvent(const TEvent& event, manager_t& manager) override;
    virtual void ShowResult() override;
};

}
}
}