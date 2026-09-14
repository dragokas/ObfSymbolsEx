// TestDLL.cpp - Implementation of test DLL
#define TESTDLL_EXPORTS
#include "TestDLL.h"
#include <cmath>
#include <stdexcept>
#include <cstring>

// MathOperations implementation
MathOperations::MathOperations() : lastResult(0.0) {}

MathOperations::MathOperations(double initialValue) : lastResult(initialValue) {}

MathOperations::~MathOperations() {}

void MathOperations::ValidateInput(double value) {
    if (std::isnan(value) || std::isinf(value)) {
        throw std::invalid_argument("Invalid input value");
    }
}

double MathOperations::Add(double a, double b) {
    lastResult = a + b;
    return lastResult;
}

double MathOperations::Subtract(double a, double b) {
    lastResult = a - b;
    return lastResult;
}

double MathOperations::Multiply(double a, double b) {
    lastResult = a * b;
    return lastResult;
}

double MathOperations::Divide(double a, double b) {
    if (b == 0.0) {
        throw std::invalid_argument("Division by zero");
    }
    lastResult = a / b;
    return lastResult;
}

double MathOperations::Power(double base, int exponent) {
    lastResult = std::pow(base, exponent);
    return lastResult;
}

double MathOperations::Sqrt(double value) {
    ValidateInput(value);
    if (value < 0) {
        throw std::invalid_argument("Cannot take square root of negative number");
    }
    lastResult = std::sqrt(value);
    return lastResult;
}

double MathOperations::Factorial(int n) {
    if (n < 0) {
        throw std::invalid_argument("Factorial of negative number");
    }
    double result = 1.0;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    lastResult = result;
    return lastResult;
}

double MathOperations::Calculate(double a, double b, char op) {
    switch (op) {
    case '+': return Add(a, b);
    case '-': return Subtract(a, b);
    case '*': return Multiply(a, b);
    case '/': return Divide(a, b);
    default: throw std::invalid_argument("Invalid operator");
    }
}

double MathOperations::Calculate(double value) {
    lastResult = value * value;
    return lastResult;
}

double MathOperations::GetLastResult() const {
    return lastResult;
}

double MathOperations::Pi() {
    return 3.14159265358979323846;
}

double MathOperations::E() {
    return 2.71828182845904523536;
}

// Point3D implementation
Point3D::Point3D() : x(0), y(0), z(0) {}

Point3D::Point3D(double x, double y, double z) : x(x), y(y), z(z) {}

double Point3D::Length() const {
    return std::sqrt(x * x + y * y + z * z);
}

Point3D Point3D::Normalize() const {
    double len = Length();
    if (len == 0) {
        return Point3D(0, 0, 0);
    }
    return Point3D(x / len, y / len, z / len);
}

Point3D Point3D::operator+(const Point3D& other) const {
    return Point3D(x + other.x, y + other.y, z + other.z);
}

Point3D Point3D::operator-(const Point3D& other) const {
    return Point3D(x - other.x, y - other.y, z - other.z);
}

Point3D Point3D::operator*(double scalar) const {
    return Point3D(x * scalar, y * scalar, z * scalar);
}

// Exported C functions
extern "C" {
    TESTDLL_API int IntegerAdd(int a, int b) {
        return a + b;
    }

    TESTDLL_API int IntegerMultiply(int a, int b) {
        return a * b;
    }

    TESTDLL_API double DoubleAdd(double a, double b) {
        return a + b;
    }

    TESTDLL_API double DoubleMultiply(double a, double b) {
        return a * b;
    }
}

// Exported C++ functions
TESTDLL_API void PrintMessage(const char* message) {
    // Implementation
}

TESTDLL_API int StringLength(const char* str) {
    return static_cast<int>(strlen(str));
}

// Template class implementation
template<typename T>
Container<T>::Container() : data(nullptr), size(0), capacity(0) {}

template<typename T>
Container<T>::~Container() {
    delete[] data;
}

template<typename T>
void Container<T>::Add(const T& item) {
    if (size >= capacity) {
        size_t newCapacity = (capacity == 0) ? 4 : capacity * 2;
        T* newData = new T[newCapacity];
        for (size_t i = 0; i < size; i++) {
            newData[i] = data[i];
        }
        delete[] data;
        data = newData;
        capacity = newCapacity;
    }
    data[size++] = item;
}

template<typename T>
T Container<T>::Get(size_t index) const {
    if (index >= size) {
        throw std::out_of_range("Index out of range");
    }
    return data[index];
}

template<typename T>
size_t Container<T>::GetSize() const {
    return size;
}

template<typename T>
void Container<T>::Clear() {
    size = 0;
}

// Explicit template instantiations
template class TESTDLL_API Container<int>;
template class TESTDLL_API Container<double>;

// ICF fixture: byte-identical trivial bodies (see TestDLL.h).
int ICFTestA::Zero() { return 0; }
int ICFTestB::Zero() { return 0; }

// Namespace implementations
namespace Geometry {
    TESTDLL_API double CircleArea(double radius) {
        return 3.14159265358979323846 * radius * radius;
    }

    TESTDLL_API double CirclePerimeter(double radius) {
        return 2.0 * 3.14159265358979323846 * radius;
    }

    TESTDLL_API double RectangleArea(double width, double height) {
        return width * height;
    }

    TESTDLL_API double TriangleArea(double base, double height) {
        return 0.5 * base * height;
    }

    namespace Advanced {
        TESTDLL_API double SphereVolume(double radius) {
            return (4.0 / 3.0) * 3.14159265358979323846 * radius * radius * radius;
        }

        TESTDLL_API double CylinderVolume(double radius, double height) {
            return 3.14159265358979323846 * radius * radius * height;
        }
    }
}

