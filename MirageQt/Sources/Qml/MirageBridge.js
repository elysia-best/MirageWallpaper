// MirageBridge.js
//
// 组件与 mirage（C++ MirageController，main.cpp 注册的全局 context property）
// 之间的薄桥接层。QML 组件里大量重复实现 value()/field()/invoke()/
// progressValue()，这里统一收敛，避免 8 处复制代码。
//
// 注意：QML 的 JS 模块函数不持有 QML 作用域，无法直接访问 context
// property（如 mirage），因此所有函数以"目标对象"为第一参数传入。
// 用法：`import "MirageBridge.js" as MirageBridge`，然后
// `MirageBridge.invoke(mirage, "downloadWorkshopItem", id)`。
.pragma library

/**
 * 从目标对象读取属性，undefined/null 时返回 fallback。
 * @param {Object} target - 目标对象（通常是 mirage 或 window）
 * @param {string} name - 属性名
 * @param {*} fallback - 属性缺失时的兜底值
 * @returns {*} 属性值或 fallback
 */
function value(target, name, fallback) {
    var result = target[name];
    return result === undefined || result === null ? fallback : result;
}

/**
 * 从数据对象读取字段，undefined/null 时返回 fallback。
 * @param {Object|null} item - 数据对象（如 WorkshopItem）
 * @param {string} name - 字段名
 * @param {*} fallback - 字段缺失时的兜底值
 * @returns {*} 字段值或 fallback
 */
function field(item, name, fallback) {
    var result = item ? item[name] : undefined;
    return result === undefined || result === null ? fallback : result;
}

/**
 * 调用目标对象上的方法，方法不存在时返回 false 不抛错。
 * @param {Object} target - 目标对象（通常是 mirage）
 * @param {string} name - 方法名
 * @param {Array<*>} [args] - 传给方法的参数数组
 * @returns {boolean} 方法是否存在并已调用
 */
function invoke(target, name, args) {
    var fn = target[name];
    if (typeof fn !== "function")
        return false;
    fn.apply(target, args || []);
    return true;
}

/**
 * 进度值归一化到 0-1：>1 视为百分比。
 * @param {number} number - 原始进度（0-1 或 0-100）
 * @returns {number} 0-1 范围内的进度
 */
function progressValue(number) {
    var value = Number(number);
    if (value > 1)
        value /= 100;
    return Math.max(0, Math.min(1, value));
}
