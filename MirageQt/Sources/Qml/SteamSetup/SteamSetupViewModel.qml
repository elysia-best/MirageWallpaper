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
    property bool busy: loginBusy
    property bool loginBusy: loginState === "loggingIn"
    property string loginState: mirage.steamLoginState
    property string loginMessage: mirage.steamLoginMessage
    property string guardType: String(mirageValue("steamGuardType", ""))
    property string qrChallengeUrl: String(mirageValue("steamQRCodeUrl", ""))
    property bool hasSavedSession: Boolean(mirageValue("steamSessionReusable", false))
        || (Boolean(mirageValue("steamLoggedIn", false)) && username.length > 0)
    property bool sessionReusable: hasSavedSession
    property bool canProceed: {
        if (currentStep === 0 || currentStep === 2)
            return true;
        return loginState === "success";
    }

    function mirageValue(name, fallback) {
        return MirageBridge.value(mirage, name, fallback);
    }

    function invoke(name) {
        return MirageBridge.invoke(mirage, name, Array.prototype.slice.call(arguments, 1));
    }

    function refreshFromService() {
        // 状态属性均为绑定，进入窗口时无需手动刷新。
    }

    function loginWithQR() {
        invoke("loginSteamQR");
    }

    function nextStep() {
        if (currentStep < 2 && canProceed)
            currentStep += 1;
    }

    function previousStep() {
        if (currentStep <= 0)
            return;
        if (currentStep === 1)
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
        // 登录已完成（loginState === "success"）；关闭由视图层处理。
    }
}
