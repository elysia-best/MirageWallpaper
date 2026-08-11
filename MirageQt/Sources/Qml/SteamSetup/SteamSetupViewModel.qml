import QtQuick
import "../MirageBridge.js" as MirageBridge

QtObject {
    id: model

    property int currentStep: 0
    property string username: mirage.steamUsername
    property string password: ""
    property string guardCode: ""
    property bool passwordVisible: false
    property bool showLog: false
    property bool busy: installBusy || loginBusy
    property bool installBusy: ["detecting", "downloading", "extracting", "initializing"].indexOf(installState) >= 0
    property bool loginBusy: loginState === "loggingIn"
        || (loginState === "waitingForGuard" && guardType === "mobileConfirm")
    property string installState: mirage.steamInstallState
    property double installProgress: mirage.steamInstallProgress
    property string installMessage: mirage.steamInstallMessage
    property string loginState: mirage.steamLoginState
    property string loginMessage: mirage.steamLoginMessage
    property string guardType: String(mirageValue("steamGuardType", ""))
    property bool sessionReusable: Boolean(mirageValue("steamSessionReusable", false))
        || (Boolean(mirageValue("steamLoggedIn", false)) && username.length > 0)
    property string sessionUsername: String(mirageValue("steamSessionUsername", username))
    property var loginLog: mirage.steamLoginLog
    property bool canProceed: {
        if (currentStep === 0 || currentStep === 3)
            return true;
        if (currentStep === 1)
            return installState === "found" || installState === "installed";
        return loginState === "success";
    }

    function mirageValue(name, fallback) {
        return MirageBridge.value(mirage, name, fallback);
    }

    function invoke(name) {
        return MirageBridge.invoke(mirage, name, Array.prototype.slice.call(arguments, 1));
    }

    function detectSteamCMD() {
        invoke("detectSteamCMD");
    }

    function nextStep() {
        if (currentStep < 3 && canProceed)
            currentStep += 1;
    }

    function previousStep() {
        if (currentStep <= 0)
            return;
        if (currentStep === 1)
            invoke("cancelSteamCMDInstallation");
        if (currentStep === 2)
            invoke("cancelSteamLogin");
        currentStep -= 1;
    }

    function cancelPendingWork() {
        invoke("cancelPendingSteamWork");
    }

    function useSavedSession() {
        invoke("useSavedSteamSession");
    }

    function completeSetup() {
        invoke("useSavedSteamSession");
    }
}
