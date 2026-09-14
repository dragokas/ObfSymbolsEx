// TestDLL.h - Header for test DLL with exported symbols
#pragma once

#ifdef TESTDLL_EXPORTS
#define TESTDLL_API __declspec(dllexport)
#else
#define TESTDLL_API __declspec(dllimport)
#endif

// Exported C++ class
class TESTDLL_API MathOperations {
private:
    double lastResult;
    
    // Private helper method
    void ValidateInput(double value);

public:
    // Constructors
    MathOperations();
    MathOperations(double initialValue);
    
    // Destructor
    ~MathOperations();
    
    // Basic operations
    double Add(double a, double b);
    double Subtract(double a, double b);
    double Multiply(double a, double b);
    double Divide(double a, double b);
    
    // Advanced operations
    double Power(double base, int exponent);
    double Sqrt(double value);
    double Factorial(int n);
    
    // Overloaded methods
    double Calculate(double a, double b, char op);
    double Calculate(double value);
    
    // Const method
    double GetLastResult() const;
    
    // Static method
    static double Pi();
    static double E();
};

// Exported struct
struct TESTDLL_API Point3D {
    double x, y, z;
    
    Point3D();
    Point3D(double x, double y, double z);
    
    double Length() const;
    Point3D Normalize() const;
    
    // Operator overloads
    Point3D operator+(const Point3D& other) const;
    Point3D operator-(const Point3D& other) const;
    Point3D operator*(double scalar) const;
};

// Exported free functions
extern "C" {
    TESTDLL_API int IntegerAdd(int a, int b);
    TESTDLL_API int IntegerMultiply(int a, int b);
    TESTDLL_API double DoubleAdd(double a, double b);
    TESTDLL_API double DoubleMultiply(double a, double b);
}

// Exported C++ functions (with name mangling)
TESTDLL_API void PrintMessage(const char* message);
TESTDLL_API int StringLength(const char* str);

// Template class (exported instantiations)
template<typename T>
class TESTDLL_API Container {
private:
    T* data;
    size_t size;
    size_t capacity;

public:
    Container();
    ~Container();
    
    void Add(const T& item);
    T Get(size_t index) const;
    size_t GetSize() const;
    void Clear();
};

// Explicit template instantiations (exported)
extern template class TESTDLL_API Container<int>;
extern template class TESTDLL_API Container<double>;

// Identical Code Folding (ICF) fixture: both methods below compile to
// byte-identical bodies, making them prime candidates for the linker's
// /OPT:ICF to fold onto a single RVA in Release builds. Exercises the
// (RVA, qualified name) keyed symbol matching that keeps each one attributed
// to its own class instead of collapsing into (or corrupting) the other.
class TESTDLL_API ICFTestA {
public:
    static int Zero();
};

class TESTDLL_API ICFTestB {
public:
    static int Zero();
};

// Namespace with exported functions
namespace Geometry {
    TESTDLL_API double CircleArea(double radius);
    TESTDLL_API double CirclePerimeter(double radius);
    TESTDLL_API double RectangleArea(double width, double height);
    TESTDLL_API double TriangleArea(double base, double height);
    
    namespace Advanced {
        TESTDLL_API double SphereVolume(double radius);
        TESTDLL_API double CylinderVolume(double radius, double height);
    }
}

