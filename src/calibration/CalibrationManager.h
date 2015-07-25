#pragma once

//ant
#include "tree/TCalibrationData.h"
#include "tree/TDataRecord.h"
#include "base/interval.h"

//
#include "base/Logger.h"

//ROOT
#include "TFile.h"
#include "TTree.h"

//std
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>

namespace ant
{


class CalibrationManager
{
private:
    const std::string cm_treename;
    const std::string cm_branchname;
    std::string dataFileName;
    std::map<std::string,std::vector<TCalibrationData>> dataBase;


    void finish() const
    {
        std::unique_ptr<TFile> dataFile(new TFile);
        TTree* cmTree = new TTree(cm_treename.c_str(),cm_treename.c_str());

        const TCalibrationData* cdata = nullptr;
        cmTree->Branch(cm_branchname.c_str(),&cdata);

        for( auto& setupList: dataBase)
            for( auto& entry: setupList.second)
            {
                cdata = std::addressof(entry);
                cmTree->Fill();
                VLOG(9) << "Stored CalibrationData"
                        << cdata
                        << "                       to tree " << dataFileName << std::endl;
            }
    }

public:
    CalibrationManager(const std::string& DataFileName):
        cm_treename("antCalibration"),
        cm_branchname("dataSets"),
        dataFileName(DataFileName)
    {

        std::unique_ptr<TFile> dataFile(new TFile);
        TTree* cmTree = nullptr;

        if ( dataFile->IsOpen() )
        {
            dataFile->GetObject(cm_treename.c_str(), cmTree);
            if (cmTree != nullptr)
            {
                TCalibrationData* cdata = nullptr;
                cmTree->SetBranchAddress(cm_branchname.c_str(),&cdata);

                for (Long64_t entry = 0 ; entry < cmTree->GetEntries() ; ++entry)
                {
                    cmTree->GetEntry(entry);
                    Add(*cdata);
                    VLOG(9) << "Adding CalibrationData "
                            << cdata;
                }

                dataFile->Close();
            }
            else
            {
                VLOG(5) << "File " << DataFileName
                        << " doesn't contain " << cm_treename << ", new will be generated.";
            }

            dataFile->Close();
        }
    }

    ~CalibrationManager()
    {
        finish();
    }

    void Add(const TCalibrationData& data)
    {
        dataBase[data.SetupID].push_back(data);
    }

    const TCalibrationData GetData(const std::string& setupID, const TID& eventID) const
    {
        auto& setupList = dataBase.at(setupID);

        for(auto& entry: setupList)
        {
            interval<TID> range(entry.FirstID,entry.LastID);
            if (range.Contains(eventID)){
                return entry;
            }
        }

        return TCalibrationData();
    }

    const std::vector<TID> GetChangePoints(const std::string& setupID) const
    {
        return {TID()};
    }

};

} //namespace ant
