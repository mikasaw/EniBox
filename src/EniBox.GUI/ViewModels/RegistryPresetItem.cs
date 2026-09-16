using System;
using CommunityToolkit.Mvvm.ComponentModel;
using EniBox.GUI.Models;

namespace EniBox.GUI.ViewModels;

/// <summary>
/// GUI 中可编辑的一条注册表预置值。类型用字符串（SZ/EXPAND_SZ/DWORD/BINARY），
/// 打包时经 PackRegistryValue.FromSpec 统一解析与校验。
/// </summary>
public partial class RegistryPresetItem : ObservableObject
{
    [ObservableProperty]
    private string _keyPath = string.Empty;

    [ObservableProperty]
    private string _valueName = string.Empty;

    [ObservableProperty]
    private string _type = "SZ";

    [ObservableProperty]
    private string _data = string.Empty;

    /// <summary>转换为打包模型。格式非法抛 ArgumentException（调用方负责呈现）。</summary>
    public PackRegistryValue ToPackValue()
    {
        return PackRegistryValue.FromSpec($"{KeyPath}|{ValueName}|{Type}|{Data}");
    }
}
