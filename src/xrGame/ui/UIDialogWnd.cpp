#include "StdAfx.h"
#include "Common/DbgTrace.hpp"
#include "UIDialogWnd.h"
#include <SDL_scancode.h>

CUIDialogWnd::CUIDialogWnd(pcstr window_name) : CUIWindow(window_name)
{
    m_pParentHolder = NULL;
    m_bWorkInPause = false;
    m_bShowMe = false;
}

CUIDialogWnd::~CUIDialogWnd() {}
void CUIDialogWnd::Show(bool status)
{
    inherited::Show(status);

    if (status)
        ResetAll();
}

bool CUIDialogWnd::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
    // XXX [foreground] DBG-PARKED-196: backspace pipeline trace
    if (dik == SDL_SCANCODE_BACKSPACE)
    {
        const bool en = IsEnabled();
        const bool wip = WorkInPause();
        const bool ignp = GetHolder() && GetHolder()->IgnorePause();
        const bool pau = Device.Paused();
        const bool irp = IR_process();
        DBG_TRACE(DBG_CAT_INPUT,
            "[3/6] entry CUIDialogWnd::OnKeyboardAction dik=%d action=%d IsEnabled=%d WorkInPause=%d "
            "GetHolder=%p IgnorePause=%d Device.Paused=%d IR_process=%d",
            dik, (int)keyboard_action, (int)en, (int)wip, (void*)GetHolder(), (int)ignp, (int)pau, (int)irp);
    }
    if (!IR_process())
        return false;
    if (inherited::OnKeyboardAction(dik, keyboard_action))
        return true;
    // XXX [foreground] DBG-PARKED-196: backspace pipeline trace
    if (dik == SDL_SCANCODE_BACKSPACE)
        DBG_TRACE(DBG_CAT_INPUT, "[3/6] fail CUIDialogWnd inherited::OnKeyboardAction returned false dik=%d", dik);
    return false;
}

bool CUIDialogWnd::OnControllerAction(int axis, const ControllerAxisState& state, EUIMessages controller_action)
{
    if (!IR_process())
        return false;
    if (inherited::OnControllerAction(axis, state, controller_action))
        return true;
    return false;
}

bool CUIDialogWnd::IR_process()
{
    if (!IsEnabled())
        return false;

    if (GetHolder() && GetHolder()->IgnorePause())
        return true;

    if (Device.Paused() && !WorkInPause())
        return false;

    return true;
}

void CUIDialogWnd::FillDebugInfo()
{
#ifndef MASTER_GOLD
    CUIWindow::FillDebugInfo();

    if (ImGui::CollapsingHeader(CUIDialogWnd::GetDebugType()))
    {
        ImGui::LabelText("Current holder", "%s", m_pParentHolder ? m_pParentHolder->GetDebugType() : "none");
        ImGui::LabelText("Work in pause", m_bWorkInPause ? "true" : "false");
    }
#endif
}

CDialogHolder* CurrentDialogHolder();

void CUIDialogWnd::ShowOrHideDialog(bool bDoHideIndicators)
{
    if (IsShown())
        GetHolder()->StopDialog(this);
    else
        CurrentDialogHolder()->StartDialog(this, bDoHideIndicators);
}

void CUIDialogWnd::ShowDialog(bool bDoHideIndicators)
{
    if (!IsShown())
        CurrentDialogHolder()->StartDialog(this, bDoHideIndicators);
}

void CUIDialogWnd::HideDialog()
{
    if (GetHolder() && IsShown())
        GetHolder()->StopDialog(this);
}
