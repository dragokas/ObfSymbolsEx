// TestApp.cpp - Test application to generate PDB with various symbol types
// This executable is used to validate ObfSymbolsEx output

#include <iostream>
#include <string>
#include <vector>

// Global functions with different signatures
void SimpleFunction() {
    std::cout << "Simple function with no parameters" << std::endl;
}

int FunctionWithReturn(int value) {
    return value * 2;
}

double FunctionWithMultipleParams(int a, double b, const char* c) {
    return a + b;
}

// Function overloads
void OverloadedFunction(int x) {
    std::cout << "Int version: " << x << std::endl;
}

void OverloadedFunction(double x) {
    std::cout << "Double version: " << x << std::endl;
}

void OverloadedFunction(const std::string& x) {
    std::cout << "String version: " << x << std::endl;
}

void OverloadedFunction(int x, int y) {
    std::cout << "Two ints: " << x << ", " << y << std::endl;
}

// Template function
template<typename T>
T TemplateFunction(T a, T b) {
    return a + b;
}

// Static function (private/internal linkage)
static void StaticFunction() {
    std::cout << "Static function" << std::endl;
}

// Inline function
inline int InlineFunction(int x, int y) {
    return x * y + x - y;
}

// Struct with methods
struct Point {
    double x, y;

    Point() : x(0), y(0) {}
    Point(double x, double y) : x(x), y(y) {}

    double Distance() const {
        return std::sqrt(x * x + y * y);
    }

    void Translate(double dx, double dy) {
        x += dx;
        y += dy;
    }

    // Operator overload
    Point operator+(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }
};

// Class with various member types
class Calculator {
private:
    double accumulator;

    // Private method
    void ValidateInput(double value) {
        if (value < 0) {
            throw std::invalid_argument("Negative values not allowed");
        }
    }

public:
    // Constructor overloads
    Calculator() : accumulator(0) {}
    Calculator(double initial) : accumulator(initial) {}

    // Destructor
    ~Calculator() {}

    // Public methods with different signatures
    double Add(double value) {
        accumulator += value;
        return accumulator;
    }

    double Subtract(double value) {
        accumulator -= value;
        return accumulator;
    }

    double Multiply(double value) {
        accumulator *= value;
        return accumulator;
    }

    double Divide(double value) {
        if (value != 0) {
            accumulator /= value;
        }
        return accumulator;
    }

    // Const method
    double GetValue() const {
        return accumulator;
    }

    // Method with multiple parameters
    double Calculate(double a, double b, char operation) {
        switch (operation) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return (b != 0) ? a / b : 0;
        default: return 0;
        }
    }

    // Static method
    static double Pi() {
        return 3.14159265359;
    }

    // Method overloads
    void Reset() {
        accumulator = 0;
    }

    void Reset(double value) {
        accumulator = value;
    }
};

// Derived class with virtual methods
class Shape {
protected:
    std::string name;

public:
    Shape(const std::string& name) : name(name) {}
    virtual ~Shape() {}

    virtual double Area() const = 0;
    virtual double Perimeter() const = 0;
    
    std::string GetName() const {
        return name;
    }
};

class Rectangle : public Shape {
private:
    double width, height;

public:
    Rectangle(double w, double h)
        : Shape("Rectangle"), width(w), height(h) {}

    // Overriding virtual destructor: exercises destructor-kind override
    // resolution (Rectangle::~Rectangle must resolve to Shape's introducing
    // destructor slot, same as Circle::~Circle below).
    ~Rectangle() override {}

    double Area() const override {
        return width * height;
    }

    double Perimeter() const override {
        return 2 * (width + height);
    }

    double GetWidth() const { return width; }
    double GetHeight() const { return height; }
};

class Circle : public Shape {
private:
    double radius;

public:
    Circle(double r) : Shape("Circle"), radius(r) {}

    ~Circle() override {}

    double Area() const override {
        return 3.14159 * radius * radius;
    }

    double Perimeter() const override {
        return 2 * 3.14159 * radius;
    }

    double GetRadius() const { return radius; }
};

// Multiple inheritance: Widget has two independent virtual base classes, so
// its Serializable sub-object sits at a non-zero offset within Widget and
// its vtable slots need a this-pointer adjustment (THIS_ADJUST) when called
// through a Serializable*. Exercises secondary-vtable shape recovery, which
// TestApp previously had no coverage for (only single inheritance via Shape).
class Renderable {
public:
    virtual ~Renderable() {}
    virtual void Render() = 0;
};

class Serializable {
public:
    virtual ~Serializable() {}
    virtual std::string Serialize() const = 0;
};

class Widget : public Renderable, public Serializable {
private:
    std::string label;

public:
    explicit Widget(const std::string& label) : label(label) {}
    ~Widget() override {}

    void Render() override {
        std::cout << "Rendering " << label << std::endl;
    }

    std::string Serialize() const override {
        return label;
    }
};

// Template class
template<typename T>
class Container {
private:
    std::vector<T> items;

public:
    void Add(const T& item) {
        items.push_back(item);
    }

    T Get(size_t index) const {
        return items[index];
    }

    size_t Size() const {
        return items.size();
    }

    void Clear() {
        items.clear();
    }
};

// Namespace with functions
namespace MathUtils {
    double Square(double x) {
        return x * x;
    }

    double Cube(double x) {
        return x * x * x;
    }

    namespace Advanced {
        double Power(double base, int exponent) {
            double result = 1.0;
            for (int i = 0; i < exponent; i++) {
                result *= base;
            }
            return result;
        }
    }
}

// Function with various parameter types
void ComplexParameters(
    int intParam,
    double doubleParam,
    const char* stringParam,
    const std::string& stringRefParam,
    std::vector<int>& vectorParam,
    void* pointerParam,
    bool boolParam
) {
    // Implementation
}

// Function with default parameters
void FunctionWithDefaults(int a, int b = 10, int c = 20) {
    std::cout << a << ", " << b << ", " << c << std::endl;
}

// Variadic function
void VariadicFunction(int count, ...) {
    // Implementation
}

// char16_t / char32_t parameters
void WideCharParameters(char16_t c16, char32_t c32) {
    // Implementation
}

// Fixed-size array parameter, passed by reference so DIA reports it as a
// true SymTagArrayType instead of the pointer it would decay to by value.
double SumFixedArray(const float (&values)[3]) {
    return values[0] + values[1] + values[2];
}

// Ordinary (non-member) function pointer parameter.
using SimpleCallback = void (*)(int);

void InvokeCallback(SimpleCallback callback, int value) {
    if (callback) {
        callback(value);
    }
}

void PrintInt(int value) {
    std::cout << "Callback got: " << value << std::endl;
}

// Pointer-to-member-function parameter, bound to a method on Calculator
// (defined earlier in this file).
using CalculatorMethod = double (Calculator::*)(double);

double InvokeCalculatorMethod(Calculator& calc, CalculatorMethod method, double value) {
    return (calc.*method)(value);
}

int main() {
    std::cout << "TestApp - Symbol Generation Test" << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << std::endl;

    // Call various functions to ensure they're included in the binary
    SimpleFunction();
    FunctionWithReturn(42);
    FunctionWithMultipleParams(1, 2.5, "test");

    OverloadedFunction(10);
    OverloadedFunction(3.14);
    OverloadedFunction(std::string("Hello"));
    OverloadedFunction(5, 7);

    // Template instantiations
    TemplateFunction<int>(1, 2);
    TemplateFunction<double>(1.5, 2.5);

    StaticFunction();
    InlineFunction(3, 4);

    // Struct usage
    Point p1(3, 4);
    Point p2(1, 2);
    Point p3 = p1 + p2;
    p1.Translate(1, 1);
    double dist = p1.Distance();

    // Class usage
    Calculator calc(100);
    calc.Add(50);
    calc.Subtract(25);
    calc.Multiply(2);
    calc.Divide(5);
    calc.Reset();
    calc.Reset(50);
    double value = calc.GetValue();
    double result = calc.Calculate(10, 5, '+');
    double pi = Calculator::Pi();

    // Derived classes
    Rectangle rect(5, 10);
    Circle circ(7);
    std::cout << "Rectangle area: " << rect.Area() << std::endl;
    std::cout << "Circle area: " << circ.Area() << std::endl;

    // Template class
    Container<int> intContainer;
    intContainer.Add(1);
    intContainer.Add(2);
    intContainer.Add(3);

    Container<std::string> stringContainer;
    stringContainer.Add("Hello");
    stringContainer.Add("World");

    // Namespace functions
    double sq = MathUtils::Square(5);
    double cube = MathUtils::Cube(3);
    double power = MathUtils::Advanced::Power(2, 10);

    FunctionWithDefaults(1);
    FunctionWithDefaults(1, 2);
    FunctionWithDefaults(1, 2, 3);

    VariadicFunction(3, 1, 2, 3);

    // Polymorphic delete through the base pointer forces the compiler to
    // emit Rectangle's/Circle's overriding (vector/scalar deleting)
    // destructors, not just their constructors.
    Shape* shapePtr1 = new Rectangle(2, 3);
    Shape* shapePtr2 = new Circle(4);
    delete shapePtr1;
    delete shapePtr2;

    // Multiple inheritance: call through both base-class pointers so both
    // vtable slots (including the THIS_ADJUST'd Serializable one) are used.
    Widget widget("TestWidget");
    Renderable* renderablePtr = &widget;
    Serializable* serializablePtr = &widget;
    renderablePtr->Render();
    std::string serialized = serializablePtr->Serialize();

    WideCharParameters(u'A', U'B');

    float coords[3] = { 1.0f, 2.0f, 3.0f };
    double sum = SumFixedArray(coords);

    InvokeCallback(PrintInt, 42);

    Calculator calc2(1.0);
    double invokedResult = InvokeCalculatorMethod(calc2, &Calculator::Add, 5.0);

    std::cout << std::endl;
    std::cout << "All functions executed successfully!" << std::endl;
    std::cout << "PDB file should contain all symbol information." << std::endl;

    return 0;
}

