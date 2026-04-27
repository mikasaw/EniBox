using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;

namespace EniBox.Tests.TestInfrastructure;

public static class CodeInspector
{
    public static bool MethodExists(Type type, string methodName, Type[]? parameterTypes = null)
    {
        if (parameterTypes == null)
        {
            return type.GetMethod(methodName) != null;
        }
        
        return type.GetMethod(methodName, parameterTypes) != null;
    }
    
    public static bool PropertyExists(Type type, string propertyName)
    {
        return type.GetProperty(propertyName) != null;
    }
    
    public static bool FieldExists(Type type, string fieldName)
    {
        return type.GetField(fieldName, BindingFlags.NonPublic | BindingFlags.Instance) != null;
    }
    
    public static List<string> GetPublicMethods(Type type)
    {
        var methods = new List<string>();
        foreach (var method in type.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly))
        {
            methods.Add(method.Name);
        }
        return methods;
    }
    
    public static List<string> GetPublicProperties(Type type)
    {
        var properties = new List<string>();
        foreach (var prop in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            properties.Add(prop.Name);
        }
        return properties;
    }
    
    public static bool IsTypeImplemented(Type interfaceType, Type implementationType)
    {
        return interfaceType.IsAssignableFrom(implementationType);
    }
    
    public static bool HasAttribute(Type type, Type attributeType)
    {
        return type.GetCustomAttribute(attributeType) != null;
    }
    
    public static List<string> FindSourceFiles(string searchDir, string searchPattern)
    {
        var files = new List<string>();
        if (Directory.Exists(searchDir))
        {
            foreach (var file in Directory.GetFiles(searchDir, searchPattern, SearchOption.AllDirectories))
            {
                files.Add(file);
            }
        }
        return files;
    }
}
