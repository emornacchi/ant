#include "EditorWindow.h"

#include "base/std_ext/string.h"
#include "base/std_ext/memory.h"
#include "calibration/gui/EditorCanvas.h"

#include "TExec.h"
#include "TApplication.h"
#include "TSystem.h"

#include "TGNumberEntry.h"
#include "TGStatusBar.h"
#include "TGButton.h"
#include "TROOT.h"

#include "base/Logger.h"
#include "TH2D.h"

#include <type_traits>
#include <sstream>
#include <iomanip>
#include <TCanvas.h>

using namespace std;

namespace ant {
namespace calibration {
namespace gui {

template<class theWidget>
class ActionWidget : public theWidget {
    struct MyExec : TExec {
        MyExec(function<void()> action_) : action(action_) {}
        virtual void Exec(const char*) override {
            action();
        }
    private:
        function<void()> action;
    };
    unique_ptr<MyExec> exec;
    bool* ptr_flag = nullptr;

public:
    using theWidget::theWidget;

    void SetAction(function<void()> action) {
        if(exec)
            return;
        exec = std_ext::make_unique<MyExec>(action);
        TQObject::Connect("Clicked()", "TExec", exec.get(), "Exec(=\"\")");
    }

    void LinkFlag(bool& flag) {
        static_assert(is_same<theWidget, TGCheckButton>::value, "LinkFlag only makes sense for check buttons");
        ptr_flag = addressof(flag);
        SetAction([this] () {
            *this->ptr_flag = this->IsOn();
        });
        SetFlag(flag);
    }

    void SetFlag(bool flag) {
        if(ptr_flag == nullptr)
            return;
        this->SetState(flag ? EButtonState::kButtonDown : EButtonState::kButtonUp);
        *this->ptr_flag = flag;
    }
};


void EditorWindow::createToolbar(TGVerticalFrame* frame)
{
    TGHorizontalFrame* frm2 = new TGHorizontalFrame(frame);
    TGHorizontalFrame* frm3 = new TGHorizontalFrame(frame);
    TGHorizontalFrame* frm4 = new TGHorizontalFrame(frame);




//    auto btn_edit = new ActionWidget<TGTextButton>(frm3,"start Editor / write data");
//    rootButton_StartEditor = btn_edit;
//    btn_edit->SetAction([this] () {
//        this->ecanvas->EditSelection();
//    });

    auto btn_avg = new ActionWidget<TGTextButton>(frm3,"AVG");
    rootButton_avg = btn_avg;
    btn_avg->SetAction([this] () {
        this->ecanvas->SetToAverage();
    });


    auto btn_saveQuit = new ActionWidget<TGTextButton>(frm4,"Save and Exit");
    rootButton_saveQuit = btn_saveQuit;
    btn_saveQuit->SetAction([this] () {
        this->editor->Save();
        gApplication->Terminate(0);
    });


    // add them all together...
    auto layout_btn = new TGLayoutHints(kLHintsLeft|kLHintsExpandX|kLHintsExpandY,2,2,2,2);
    auto add_to_frame = [this, layout_btn] (TGHorizontalFrame* frm, TGWidget* widget) {
        frm->AddFrame(dynamic_cast<TGFrame*>(widget), layout_btn);
    };

    add_to_frame(frm3, btn_avg);
    add_to_frame(frm4, btn_saveQuit);

    auto layout_frm =  new TGLayoutHints(kLHintsBottom | kLHintsExpandX);
    frame->AddFrame(frm4, layout_frm);
    frame->AddFrame(frm3, layout_frm);
    frame->AddFrame(frm2, layout_frm);
}

void EditorWindow::updateLayout()
{
    // Map all subwindows of main frame
    MapSubwindows();
    Resize(GetDefaultSize()); // this is used here to init layout algorithm
    MapWindow();
}

EditorWindow::EditorWindow(const string& filename) :
    TGMainFrame(gClient->GetRoot()),
    editor(make_shared<ant::calibration::Editor>(filename))
{
    // Set a name to the main frame
    SetWindowName( (std_ext::formatter() << "Ant-calib Editor: " << filename).str().c_str() );

//    editor->AddFromFolder(filename);

    TGVerticalFrame* frame = new TGVerticalFrame(this);

    // Create a horizontal frame widget with buttons

    frame_canvas = new TGHorizontalFrame(frame);
    frame->AddFrame(frame_canvas, new TGLayoutHints(kLHintsExpandY | kLHintsExpandX));

    ecanvas = new EmbeddedEditorCanvas(this,frame_canvas);
    frame_canvas->AddFrame(ecanvas, new TGLayoutHints(kLHintsExpandY | kLHintsExpandX));
    updateLayout();

    AddFrame(frame, new TGLayoutHints(kLHintsExpandX | kLHintsExpandY));

    createToolbar(frame);


    AddInput(kKeyPressMask | kKeyReleaseMask);
    UpdateMe();

    // set focus
    gVirtualX->SetInputFocus(GetId());

}

Bool_t EditorWindow::HandleKey(Event_t* event) {

    if (event->fType == kGKeyPress) {
        char input[10];
        UInt_t keysym;
        gVirtualX->LookupString(event, input, sizeof(input), keysym);

        auto it_key = keys.find((EKeySym)keysym);
        if(it_key != keys.end()) {
            it_key->second->Clicked();
            return kTRUE;
        }
    }
    return TGMainFrame::HandleKey(event);
}

EditorWindow::~EditorWindow()
{
    // executed if window is closed
    gApplication->Terminate(0);
}

std::shared_ptr<Editor> EditorWindow::GetEditor() { return editor;}



void EditorWindow::UpdateMe()
{
    updateLayout();
}



}}} // namespace ant::calibration::gui

