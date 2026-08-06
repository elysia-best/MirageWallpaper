import QtQuick

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
        var value = mirage[name];
        return value === undefined || value === null ? fallback : value;
    }

    function invoke(name) {
        var fn = mirage[name];
        if (typeof fn !== "function")
            return false;
        var args = Array.prototype.slice.call(arguments, 1);
        fn.apply(mirage, args);
        return true;
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
