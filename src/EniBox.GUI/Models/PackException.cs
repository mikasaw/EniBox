using System;

namespace EniBox.GUI.Models
{
    public class PackException : Exception
    {
        public int ErrorCode { get; }

        public PackException(int errorCode, string message) : base(message)
        {
            ErrorCode = errorCode;
        }

        public PackException(int errorCode, string message, Exception innerException)
            : base(message, innerException)
        {
            ErrorCode = errorCode;
        }
    }
}
