
#include <cstdint>
#include <cmath>
#include <string>
#include <cstring>
#include <array>
#include <stdexcept>
#include <type_traits>
#include <limits>

//==============================================================================
/// Auto-generated C++ class for the 'Filter' processor
///

#if ! (defined (__cplusplus) && (__cplusplus >= 201703L))
 #error "This code requires that your compiler is set to use C++17 or later!"
#endif

struct Filter
{
    Filter() = default;
    ~Filter() = default;

    static constexpr std::string_view name = "Filter";

    //==============================================================================
    using EndpointHandle = uint32_t;

    enum class EndpointType
    {
        stream,
        event,
        value
    };

    struct EndpointInfo
    {
        uint32_t handle;
        const char* name;
        EndpointType endpointType;
    };

    //==============================================================================
    static constexpr uint32_t numInputEndpoints  = 3;
    static constexpr uint32_t numOutputEndpoints = 1;

    static constexpr uint32_t maxFramesPerBlock  = 512;
    static constexpr uint32_t eventBufferSize    = 32;
    static constexpr uint32_t maxOutputEventSize = 0;
    static constexpr double   latency            = 0.000000;

    enum class EndpointHandles
    {
        frequency = 1,
        q         = 2,
        in        = 3,
        out       = 4
    };

    static constexpr uint32_t getEndpointHandleForName (std::string_view endpointName)
    {
        if (endpointName == "frequency")  return static_cast<uint32_t> (EndpointHandles::frequency);
        if (endpointName == "q")          return static_cast<uint32_t> (EndpointHandles::q);
        if (endpointName == "in")         return static_cast<uint32_t> (EndpointHandles::in);
        if (endpointName == "out")        return static_cast<uint32_t> (EndpointHandles::out);
        return 0;
    }

    static constexpr EndpointInfo inputEndpoints[] =
    {
        { 1,  "frequency",  EndpointType::event  },
        { 2,  "q",          EndpointType::event  },
        { 3,  "in",         EndpointType::stream }
    };

    static constexpr EndpointInfo outputEndpoints[] =
    {
        { 4,  "out",  EndpointType::stream }
    };

    //==============================================================================
    static constexpr uint32_t numAudioInputChannels  = 2;
    static constexpr uint32_t numAudioOutputChannels = 2;

    static constexpr std::array outputAudioStreams
    {
        outputEndpoints[0]
    };

    static constexpr std::array<EndpointInfo, 0> outputEvents {};

    static constexpr std::array<EndpointInfo, 0> outputMIDIEvents {};

    static constexpr std::array inputAudioStreams
    {
        inputEndpoints[2]
    };

    static constexpr std::array inputEvents
    {
        inputEndpoints[0],
        inputEndpoints[1]
    };

    static constexpr std::array<EndpointInfo, 0> inputMIDIEvents {};

    static constexpr std::array inputParameters
    {
        inputEndpoints[0],
        inputEndpoints[1]
    };

    static constexpr const char* programDetailsJSON =
            "{\n"
            "  \"mainProcessor\": \"Filter\",\n"
            "  \"inputs\": [\n"
            "    {\n"
            "      \"endpointID\": \"frequency\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"mid\": 1000,\n"
            "        \"name\": \"Frequency\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 20000,\n"
            "        \"init\": 1000,\n"
            "        \"unit\": \"Hz\"\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"q\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Q\",\n"
            "        \"min\": 0.01,\n"
            "        \"max\": 100,\n"
            "        \"init\": 0.707107\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"in\",\n"
            "      \"endpointType\": \"stream\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"vector\",\n"
            "        \"element\": {\n"
            "          \"type\": \"float32\"\n"
            "        },\n"
            "        \"size\": 2\n"
            "      },\n"
            "      \"purpose\": \"audio in\",\n"
            "      \"numAudioChannels\": 2\n"
            "    }\n"
            "  ],\n"
            "  \"outputs\": [\n"
            "    {\n"
            "      \"endpointID\": \"out\",\n"
            "      \"endpointType\": \"stream\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"vector\",\n"
            "        \"element\": {\n"
            "          \"type\": \"float32\"\n"
            "        },\n"
            "        \"size\": 2\n"
            "      },\n"
            "      \"purpose\": \"audio out\",\n"
            "      \"numAudioChannels\": 2\n"
            "    }\n"
            "  ]\n"
            "}";

    //==============================================================================
    struct intrinsics
    {
        template <typename T> static constexpr bool isIntType()     { return std::is_integral<T>::value && ! std::is_same<T, bool>::value; }
        template <typename T> using UnsignedVersion = typename std::make_unsigned<T>::type;

        template <typename T> static constexpr T add (T a, T b)
        {
            if constexpr (isIntType<T>())
                return static_cast<T> (static_cast<UnsignedVersion<T>> (a) + static_cast<UnsignedVersion<T>> (b));
            else
                return a + b;
        }

        template <typename T> static constexpr T subtract (T a, T b)
        {
            if constexpr (isIntType<T>())
                return static_cast<T> (static_cast<UnsignedVersion<T>> (a) - static_cast<UnsignedVersion<T>> (b));
            else
                return a - b;
        }

        template <typename T> static constexpr T multiply (T a, T b)
        {
            if constexpr (isIntType<T>())
                return static_cast<T> (static_cast<UnsignedVersion<T>> (a) * static_cast<UnsignedVersion<T>> (b));
            else
                return a * b;
        }

        template <typename T> static constexpr T leftShift (T a, T b)
        {
            if constexpr (isIntType<T>())
                return static_cast<T> (static_cast<UnsignedVersion<T>> (a) << b);
            else
                return a << b;
        }

        template <typename T> static constexpr void addTo (T& a, int32_t b)
        {
            a = add (a, static_cast<T> (b));
        }

        template <typename T> static constexpr T negate (T a)
        {
            if constexpr (isIntType<T>())
                return static_cast<T> (static_cast<UnsignedVersion<T>> (0) - static_cast<UnsignedVersion<T>> (a));
            else
                return -a;
        }

        template <typename T> static T modulo (T a, T b)
        {
            if constexpr (std::is_floating_point<T>::value)
                return std::fmod (a, b);
            else
                return a % b;
        }

        template <typename T> static T addModulo2Pi (T a, T b)
        {
            constexpr auto twoPi = static_cast<T> (3.141592653589793238 * 2);
            auto n = a + b;
            return n >= twoPi ? std::remainder (n, twoPi) : n;
        }

        template <typename T> static T abs           (T a)              { return std::abs (a); }
        template <typename T> static T min           (T a, T b)         { return std::min (a, b); }
        template <typename T> static T max           (T a, T b)         { return std::max (a, b); }
        template <typename T> static T clamp         (T a, T b, T c)    { return a < b ? b : (a > c ? c : a); }
        template <typename T> static T wrap          (T a, T b)         { if (b == 0) return 0; auto n = modulo (a, b); if (n < 0) n += b; return n; }
        template <typename T> static T fmod          (T a, T b)         { return b != 0 ? std::fmod (a, b) : 0; }
        template <typename T> static T remainder     (T a, T b)         { return b != 0 ? std::remainder (a, b) : 0; }
        template <typename T> static T floor         (T a)              { return std::floor (a); }
        template <typename T> static T ceil          (T a)              { return std::ceil (a); }
        template <typename T> static T rint          (T a)              { return std::rint (a); }
        template <typename T> static T sqrt          (T a)              { return std::sqrt (a); }
        template <typename T> static T pow           (T a, T b)         { return std::pow (a, b); }
        template <typename T> static T exp           (T a)              { return std::exp (a); }
        template <typename T> static T log           (T a)              { return std::log (a); }
        template <typename T> static T log10         (T a)              { return std::log10 (a); }
        template <typename T> static T sin           (T a)              { return std::sin (a); }
        template <typename T> static T cos           (T a)              { return std::cos (a); }
        template <typename T> static T tan           (T a)              { return std::tan (a); }
        template <typename T> static T sinh          (T a)              { return std::sinh (a); }
        template <typename T> static T cosh          (T a)              { return std::cosh (a); }
        template <typename T> static T tanh          (T a)              { return std::tanh (a); }
        template <typename T> static T asinh         (T a)              { return std::asinh (a); }
        template <typename T> static T acosh         (T a)              { return std::acosh (a); }
        template <typename T> static T atanh         (T a)              { return std::atanh (a); }
        template <typename T> static T asin          (T a)              { return std::asin (a); }
        template <typename T> static T acos          (T a)              { return std::acos (a); }
        template <typename T> static T atan          (T a)              { return std::atan (a); }
        template <typename T> static T atan2         (T a, T b)         { return std::atan2 (a, b); }
        template <typename T> static T isnan         (T a)              { return std::isnan (a) ? 1 : 0; }
        template <typename T> static T isinf         (T a)              { return std::isinf (a) ? 1 : 0; }
        template <typename T> static T select        (bool c, T a, T b) { return c ? a : b; }

        static int32_t reinterpretFloatToInt (float   a)                { int32_t i; memcpy (std::addressof(i), std::addressof(a), sizeof(i)); return i; }
        static int64_t reinterpretFloatToInt (double  a)                { int64_t i; memcpy (std::addressof(i), std::addressof(a), sizeof(i)); return i; }
        static float   reinterpretIntToFloat (int32_t a)                { float   f; memcpy (std::addressof(f), std::addressof(a), sizeof(f)); return f; }
        static double  reinterpretIntToFloat (int64_t a)                { double  f; memcpy (std::addressof(f), std::addressof(a), sizeof(f)); return f; }

        static int32_t rightShiftUnsigned (int32_t a, int32_t b)        { return static_cast<int32_t> (static_cast<uint32_t> (a) >> b); }
        static int64_t rightShiftUnsigned (int64_t a, int64_t b)        { return static_cast<int64_t> (static_cast<uint64_t> (a) >> b); }

        struct VectorOps
        {
            template <typename Vec> static Vec abs     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::abs (x); }); }
            template <typename Vec> static Vec min     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::min (x, y); }); }
            template <typename Vec> static Vec max     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::max (x, y); }); }
            template <typename Vec> static Vec sqrt    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sqrt (x); }); }
            template <typename Vec> static Vec log     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::log (x); }); }
            template <typename Vec> static Vec log10   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::log10 (x); }); }
            template <typename Vec> static Vec sin     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sin (x); }); }
            template <typename Vec> static Vec cos     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::cos (x); }); }
            template <typename Vec> static Vec tan     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::tan (x); }); }
            template <typename Vec> static Vec sinh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sinh (x); }); }
            template <typename Vec> static Vec cosh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::cosh (x); }); }
            template <typename Vec> static Vec tanh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::tanh (x); }); }
            template <typename Vec> static Vec asinh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::asinh (x); }); }
            template <typename Vec> static Vec acosh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::acosh (x); }); }
            template <typename Vec> static Vec atanh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::atanh (x); }); }
            template <typename Vec> static Vec asin    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::asin (x); }); }
            template <typename Vec> static Vec acos    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::acos (x); }); }
            template <typename Vec> static Vec atan    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::atan (x); }); }
            template <typename Vec> static Vec atan2   (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::atan2 (x, y); }); }
            template <typename Vec> static Vec pow     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::pow (x, y); }); }
            template <typename Vec> static Vec exp     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::exp (x); }); }

            template <typename Vec> static Vec rightShiftUnsigned (Vec a, Vec b) { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::rightShiftUnsigned (x, y); }); }
        };
    };

    //==============================================================================
    static constexpr float  _inf32  =  std::numeric_limits<float>::infinity();
    static constexpr double _inf64  =  std::numeric_limits<double>::infinity();
    static constexpr float  _ninf32 = -std::numeric_limits<float>::infinity();
    static constexpr double _ninf64 = -std::numeric_limits<double>::infinity();
    static constexpr float  _nan32  =  std::numeric_limits<float>::quiet_NaN();
    static constexpr double _nan64  =  std::numeric_limits<double>::quiet_NaN();

    using SizeType = int32_t;
    using IndexType = int32_t;
    using StringHandle = uint32_t;

    struct Null
    {
        template <typename AnyType> operator AnyType() const    { return {}; }
        Null operator[] (IndexType) const                       { return {}; }
    };

    //==============================================================================
    template <typename ElementType, SizeType numElements>
    struct Array
    {
        Array() = default;
        Array (Null) {}
        Array (const Array&) = default;

        template <typename ElementOrList>
        Array (const ElementOrList& value) noexcept
        {
            if constexpr (std::is_convertible<ElementOrList, ElementType>::value)
            {
                for (IndexType i = 0; i < numElements; ++i)
                    this->elements[i] = static_cast<ElementType> (value);
            }
            else
            {
                for (IndexType i = 0; i < numElements; ++i)
                    this->elements[i] = static_cast<ElementType> (value[i]);
            }
        }

        template <typename... Others>
        Array (ElementType e0, ElementType e1, Others... others) noexcept
        {
            this->elements[0] = static_cast<ElementType> (e0);
            this->elements[1] = static_cast<ElementType> (e1);

            if constexpr (numElements > 2)
            {
                const ElementType initialisers[] = { static_cast<ElementType> (others)... };

                for (size_t i = 0; i < sizeof...(others); ++i)
                    this->elements[i + 2] = initialisers[i];
            }
        }

        Array (const ElementType* rawArray, size_t) noexcept
        {
            for (IndexType i = 0; i < numElements; ++i)
                this->elements[i] = rawArray[i];
        }

        Array& operator= (const Array&) noexcept = default;
        Array& operator= (Null) noexcept                 { this->clear(); return *this; }

        template <typename ElementOrList>
        Array& operator= (const ElementOrList& value) noexcept
        {
            if constexpr (std::is_convertible<ElementOrList, ElementType>::value)
            {
                for (IndexType i = 0; i < numElements; ++i)
                    this->elements[i] = static_cast<ElementType> (value);
            }
            else
            {
                for (IndexType i = 0; i < numElements; ++i)
                    this->elements[i] = static_cast<ElementType> (value[i]);
            }
        }

        static constexpr SizeType size()                                    { return numElements; }

        const ElementType& operator[] (IndexType index) const noexcept      { return this->elements[index]; }
        ElementType& operator[] (IndexType index) noexcept                  { return this->elements[index]; }

        void clear() noexcept
        {
            for (auto& element : elements)
                element = ElementType();
        }

        void clear (SizeType numElementsToClear) noexcept
        {
            for (SizeType i = 0; i < numElementsToClear; ++i)
                elements[i] = ElementType();
        }

        ElementType elements[numElements] = {};
    };

    //==============================================================================
    template <typename ElementType, SizeType numElements>
    struct Vector  : public Array<ElementType, numElements>
    {
        Vector() = default;
        Vector (Null) {}

        template <typename ElementOrList>
        Vector (const ElementOrList& value) noexcept  : Array<ElementType, numElements> (value) {}

        template <typename... Others>
        Vector (ElementType e0, ElementType e1, Others... others) noexcept  : Array<ElementType, numElements> (e0, e1, others...) {}

        Vector (const ElementType* rawArray, size_t) noexcept  : Array<ElementType, numElements> (rawArray, size_t()) {}

        template <typename ElementOrList>
        Vector& operator= (const ElementOrList& value) noexcept { return Array<ElementType, numElements>::operator= (value); }

        Vector& operator= (Null) noexcept { this->clear(); return *this; }

        operator ElementType() const noexcept
        {
            static_assert (numElements == 1);
            return this->elements[0];
        }

        constexpr auto operator!() const noexcept     { return performUnaryOp ([] (ElementType n) { return ! n; }); }
        constexpr auto operator~() const noexcept     { return performUnaryOp ([] (ElementType n) { return ~n; }); }
        constexpr auto operator-() const noexcept     { return performUnaryOp ([] (ElementType n) { return intrinsics::negate (n); }); }

        constexpr auto operator+ (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return intrinsics::add (a, b); }); }
        constexpr auto operator- (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return intrinsics::subtract (a, b); }); }
        constexpr auto operator* (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return intrinsics::multiply (a, b); }); }
        constexpr auto operator/ (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a / b; }); }
        constexpr auto operator% (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return intrinsics::modulo (a, b); }); }
        constexpr auto operator& (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a & b; }); }
        constexpr auto operator| (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a | b; }); }
        constexpr auto operator<< (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return intrinsics::leftShift (a, b); }); }
        constexpr auto operator>> (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a >> b; }); }

        constexpr auto operator== (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a == b; }); }
        constexpr auto operator!= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a != b; }); }
        constexpr auto operator<  (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a < b; }); }
        constexpr auto operator<= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a <= b; }); }
        constexpr auto operator>  (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a > b; }); }
        constexpr auto operator>= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a >= b; }); }

        template <typename Functor>
        constexpr Vector performUnaryOp (Functor&& f) const noexcept
        {
            Vector result;

            for (IndexType i = 0; i < numElements; ++i)
                result.elements[i] = f (this->elements[i]);

            return result;
        }

        template <typename Functor>
        constexpr Vector performBinaryOp (const Vector& rhs, Functor&& f) const noexcept
        {
            Vector result;

            for (IndexType i = 0; i < numElements; ++i)
                result.elements[i] = f (this->elements[i], rhs.elements[i]);

            return result;
        }

        template <typename Functor>
        constexpr Vector<bool, numElements> performComparison (const Vector& rhs, Functor&& f) const noexcept
        {
            Vector<bool, numElements> result;

            for (IndexType i = 0; i < numElements; ++i)
                result.elements[i] = f (this->elements[i], rhs.elements[i]);

            return result;
        }
    };

    //==============================================================================
    template <typename ElementType>
    struct Slice
    {
        Slice() = default;
        Slice (Null) {}
        Slice (ElementType* e, SizeType size) : elements (e), numElements (size) {}
        Slice (const Slice&) = default;
        Slice& operator= (const Slice&) = default;
        template <typename ArrayType> Slice (const ArrayType& a) : elements (const_cast<ArrayType&> (a).elements), numElements (a.size()) {}
        template <typename ArrayType> Slice (const ArrayType& a, SizeType offset, SizeType size) : elements (const_cast<ArrayType&> (a).elements + offset), numElements (size) {}

        constexpr SizeType size() const                                     { return numElements; }
        ElementType operator[] (IndexType index) const noexcept             { return numElements == 0 ? ElementType() : elements[index]; }
        ElementType& operator[] (IndexType index) noexcept                  { return numElements == 0 ? emptyValue : elements[index]; }

        Slice slice (IndexType start, IndexType end) noexcept
        {
            if (numElements == 0) return {};
            if (start >= numElements) return {};

            return { elements + start, std::min (static_cast<SizeType> (end - start), numElements - start) };
        }

        const Slice slice (IndexType start, IndexType end) const noexcept
        {
            if (numElements == 0) return {};
            if (start >= numElements) return {};

            return { elements + start, std::min (static_cast<SizeType> (end - start), numElements - start) };
        }

        ElementType* elements = nullptr;
        SizeType numElements = 0;

        static inline ElementType emptyValue {};
    };

    //==============================================================================
    #if __clang__
     #pragma clang diagnostic push
     #pragma clang diagnostic ignored "-Wunused-variable"
     #pragma clang diagnostic ignored "-Wunused-parameter"
     #pragma clang diagnostic ignored "-Wunused-label"
     #pragma clang diagnostic ignored "-Wtautological-compare"
     #pragma clang diagnostic ignored "-Wunknown-warning-option"

     #if __clang_major__ >= 14
      #pragma clang diagnostic ignored "-Wunused-but-set-variable"
     #endif

     #if __clang_major__ >= 17
      #pragma clang diagnostic ignored "-Wnan-infinity-disabled"
     #endif

    #elif __GNUC__
     #pragma GCC diagnostic push
     #pragma GCC diagnostic ignored "-Wunused-variable"
     #pragma GCC diagnostic ignored "-Wunused-parameter"
     #pragma GCC diagnostic ignored "-Wunused-but-set-variable"
     #pragma GCC diagnostic ignored "-Wunused-label"
    #else
     #pragma warning (push, 0)
     #pragma warning (disable: 4702)
     #pragma warning (disable: 4706)
    #endif

    //==============================================================================
    using std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_CoefficientType = float;
    struct std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation
    {
        float d = {};
        float a = {};
        float g1 = {};
        Array<Vector<float, 2>, 2> z;
        int32_t mode = {};
    };

    struct std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_State
    {
        std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation filter;
        bool updateFilter = {};
        float filterFrequency = {};
        float filterQ = {};
        int32_t _count = {};
        int32_t _resumeIndex = {};
    };

    struct _Filter_State
    {
        int32_t _sessionID = {};
        double _frequency = {};
        std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_State filter;
    };

    struct Filter_State
    {
        int32_t _currentFrame = {};
        _Filter_State _state;
    };

    struct Filter_IO
    {
        Array<Vector<float, 2>, 512> in;
        Array<Vector<float, 2>, 512> out;
    };

    struct _Filter_IO
    {
        Vector<float, 2> in;
        Vector<float, 2> out;
    };

    using std_intrinsics_T = double;
    using std_intrinsics_T_0 = double;
    using std_intrinsics_T_1 = double;
    using std_intrinsics_T_2 = double;
    using std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_FrameType = Vector<float, 2>;

    //==============================================================================
    double getMaxFrequency() const
    {
        return 192000.0;
    }

    void initialise (int32_t sessionID, double frequency)
    {
        if (frequency > getMaxFrequency()) throw std::runtime_error ("frequency out of range");
        initSessionID = sessionID;
        initFrequency = frequency;
        reset();
    }

    void reset()
    {
        std::memset (reinterpret_cast<char*> (&cmajState), 0, sizeof (cmajState));
        int32_t processorID = 0;
        _initialise (cmajState, processorID, initSessionID, initFrequency);
    }

    void advance (int32_t frames)
    {
        cmajIO.out.clear (static_cast<SizeType> (frames));
        _advance (cmajState, cmajIO, frames);
    }

    void copyOutputValue (EndpointHandle endpointHandle, void* dest)
    {
        (void) endpointHandle; (void) dest;

        throw std::runtime_error ("Unknown value endpointHandle:" + std::to_string (endpointHandle));
    }

    void copyOutputFrames (EndpointHandle endpointHandle, void* dest, uint32_t numFramesToCopy)
    {
        if (endpointHandle == 4) { std::memcpy (reinterpret_cast<char*> (dest), std::addressof (cmajIO.out), 8 * numFramesToCopy); std::memset (reinterpret_cast<char*> (std::addressof (cmajIO.out)), 0, 8 * numFramesToCopy); return; }
        throw std::runtime_error ("Unknown stream endpointHandle:" + std::to_string (endpointHandle));
    }

    uint32_t getNumOutputEvents (EndpointHandle endpointHandle)
    {
        (void) endpointHandle;

        throw std::runtime_error ("Unknown event endpointHandle:" + std::to_string (endpointHandle));
        return {};
    }

    void resetOutputEventCount (EndpointHandle endpointHandle)
    {
        (void) endpointHandle;
    }

    uint32_t getOutputEventType (EndpointHandle endpointHandle, uint32_t index)
    {
        (void) endpointHandle; (void) index;

        throw std::runtime_error ("Unknown event endpointHandle:" + std::to_string (endpointHandle));
        return {};
    }

    static uint32_t getOutputEventDataSize (EndpointHandle endpointHandle, uint32_t typeIndex)
    {
        (void) endpointHandle; (void) typeIndex;

        throw std::runtime_error ("Unknown event endpointHandle:" + std::to_string (endpointHandle));
        return 0;
    }

    uint32_t readOutputEvent (EndpointHandle endpointHandle, uint32_t index, unsigned char* dest)
    {

        (void) endpointHandle; (void) index; (void) dest;

        throw std::runtime_error ("Unknown event endpointHandle:" + std::to_string (endpointHandle));
        return {};
    }

    void addEvent_frequency (float event)
    {
        _sendEvent_frequency (cmajState, event);
    }

    void addEvent_q (float event)
    {
        _sendEvent_q (cmajState, event);
    }

    void addEvent (EndpointHandle endpointHandle, uint32_t typeIndex, const unsigned char* eventData)
    {
        (void) endpointHandle; (void) typeIndex; (void) eventData;

        if (endpointHandle == 1)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_frequency (value);
        }

        if (endpointHandle == 2)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_q (value);
        }
    }

    void setValue (EndpointHandle endpointHandle, const void* value, int32_t frames)
    {
        (void) endpointHandle; (void) value; (void) frames;
    }

    void setInputFrames_in (const void* data, uint32_t numFrames, uint32_t numTrailingFramesToClear)
    {
        memcpy (cmajIO.in.elements, data, numFrames * 8);
        if (numTrailingFramesToClear != 0) memset (reinterpret_cast<char*> (cmajIO.in.elements + numFrames), 0, numTrailingFramesToClear * 8);
    }

    void setInputFrames (EndpointHandle endpointHandle, const void* frameData, uint32_t numFrames, uint32_t numTrailingFramesToClear)
    {
        if (endpointHandle == 3) return setInputFrames_in (frameData, numFrames, numTrailingFramesToClear);
    }

    //==============================================================================
    // Rendering state values
    int32_t initSessionID;
    double initFrequency;
    Filter_State cmajState = {};
    Filter_IO cmajIO = {};

    //==============================================================================
    void _sendEvent_frequency (Filter_State& _state, float value) noexcept
    {
        _Filter__frequency (_state._state, value);
    }

    void _Filter__frequency (_Filter_State& _state, float value) noexcept
    {
        std__filters__tpt__svf__Processor__frequency (_state.filter, value);
    }

    void std__filters__tpt__svf__Processor__frequency (std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_State& _state, float f) noexcept
    {
        _state.updateFilter = true;
        _state.filterFrequency = f;
    }

    void _sendEvent_q (Filter_State& _state, float value) noexcept
    {
        _Filter__q (_state._state, value);
    }

    void _Filter__q (_Filter_State& _state, float value) noexcept
    {
        std__filters__tpt__svf__Processor__q (_state.filter, value);
    }

    void std__filters__tpt__svf__Processor__q (std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_State& _state, float q) noexcept
    {
        _state.updateFilter = true;
        _state.filterQ = q;
    }

    void _initialise (Filter_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        _Filter___initialise (_state._state, processorID, sessionID, frequency);
    }

    void _Filter___initialise (_Filter_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        std__filters__tpt__svf__Processor___initialise (_state.filter, processorID, sessionID, frequency);
    }

    void std__filters__tpt__svf__Processor___initialise (std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        _state.updateFilter = false;
        _state.filterFrequency = g_initialFrequency;
        _state.filterQ = g_initialQ;
        _state.filter = std__filters__tpt__svf__create (g_initialMode, 1.0 * g__frequency, 1000.0, 0.7071070075035095);
    }

    std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation std__filters__tpt__svf__create (int32_t mode, double processorFrequency, double filterFrequency, double Q) noexcept
    {
        std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation  filter;

        filter = std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation {};
        std__filters__tpt__svf__setMode (filter, mode);
        std__filters__tpt__svf__setFrequency (filter, processorFrequency, filterFrequency, Q);
        return filter;
    }

    void std__filters__tpt__svf__setMode (std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation& this_, int32_t mode) noexcept
    {
        this_.mode = mode;
    }

    void std__filters__tpt__svf__setFrequency (std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation& this_, double processorFrequency, double filterFrequency, double Q) noexcept
    {
        double  period;
        double  f;
        double  invQ;

        filterFrequency = intrinsics::clamp (filterFrequency, 0.0, processorFrequency * 0.48);
        period = 0.5 / processorFrequency;
        f = ((2.0 * processorFrequency) * intrinsics::tan ((6.283185307179586 * filterFrequency) * period)) * period;
        invQ = 1.0 / Q;
        this_.d = static_cast<float> (1.0 / ((1.0 + (invQ * f)) + (f * f)));
        this_.a = static_cast<float> (f);
        this_.g1 = static_cast<float> (f + invQ);
    }

    double std__intrinsics__clamp (double value, double minimum, double maximum) noexcept
    {
        {
            return (value > maximum) ? maximum : ((value < minimum) ? minimum : value);
        }
    }

    double std__intrinsics__tan (double n) noexcept
    {
        {
            return intrinsics::sin (n) / intrinsics::cos (n);
        }
    }

    double std__intrinsics__sin (double n) noexcept
    {
        {
            return 0.0;
        }
    }

    double std__intrinsics__cos (double n) noexcept
    {
        {
            return 0.0;
        }
    }

    void _advance (Filter_State& _state, Filter_IO& _io, int32_t _frames) noexcept
    {
        _Filter_IO  ioCopy;

        for (;;)
        {
            if (_state._currentFrame == _frames)
            {
                break;
            }
            ioCopy = _Filter_IO {};
            ioCopy.in = _io.in[_state._currentFrame];
            main (_state._state, ioCopy);
            _io.out[_state._currentFrame] = ioCopy.out;
            intrinsics::addTo (_state._currentFrame, 1);
        }
        _state._currentFrame = int32_t {0};
    }

    void main (_Filter_State& _state, _Filter_IO& _io) noexcept
    {
        _Filter_IO  filter_io;

        filter_io = _Filter_IO {};
        {
            filter_io.in = (filter_io.in + _io.in);
        }
        std__filters__tpt__svf__Processor__main (_state.filter, filter_io);
        {
            _io.out = (_io.out + filter_io.out);
        }
    }

    void std__filters__tpt__svf__Processor__main (std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_State& _state, _Filter_IO& _io) noexcept
    {
        Vector<float, 2>  _temp;

        switch (_state._resumeIndex)
        {
            case 1: goto _branch0_0;
            default: break;
        }

        for (;;)
        {
            if (_state.updateFilter)
            {
                std__filters__tpt__svf__setMode (_state.filter, g_filterMode);
                std__filters__tpt__svf__setFrequency (_state.filter, 1.0 * g__frequency, static_cast<double> (_state.filterFrequency), static_cast<double> (_state.filterQ));
                _state.updateFilter = false;
            }
            {
                _state._count = g_framesPerParameterUpdate;
                _branch0_0:
                {
                    for (;;)
                    {
                        intrinsics::addTo (_state._count, -1);
                        if (_state._count < int32_t {0})
                        {
                            goto _break_0;
                        }
                        _temp = _io.out;
                        _io.out = (_temp + std__filters__tpt__svf__process (_state.filter, _io.in));
                        _state._resumeIndex = int32_t {1};
                        return;
                    }
                }
                _break_0: {}
            }
        }
    }

    Vector<float, 2> std__filters__tpt__svf__process (std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation& this_, Vector<float, 2> x) noexcept
    {
        Array<Vector<float, 2>, 3>  result;

        result = std__filters__tpt__svf__processMultimode (this_, x);
        if (this_.mode == g_lowPass)
        {
            return result[int32_t {0}];
        }
        if (this_.mode == g_highPass)
        {
            return result[int32_t {1}];
        }
        return result[int32_t {2}];
    }

    Array<Vector<float, 2>, 3> std__filters__tpt__svf__processMultimode (std_std_filters_specialised_vec_2_f32_f32_3_f9E65b_tpt_svf_std_std_filters_specialised_vec_2_f32_f_fiO6qe_1_Implementation& this_, Vector<float, 2> x) noexcept
    {
        Vector<float, 2>  _temp;
        float  _temp_0;
        Vector<float, 2>  _temp_1;
        float  _temp_2;
        Vector<float, 2>  highValue;
        Vector<float, 2>  _temp_3;
        float  _temp_4;
        Vector<float, 2>  bandValue;
        Vector<float, 2>  _temp_5;
        float  _temp_6;
        Vector<float, 2>  lowValue;
        Vector<float, 2>  _temp_7;
        float  _temp_8;
        Vector<float, 2>  _temp_9;
        float  _temp_10;
        Array<Vector<float, 2>, 3>  _temp_11;

        _temp_0 = this_.g1;
        _temp[int32_t {0}] = _temp_0;
        _temp[int32_t {1}] = _temp_0;
        _temp_2 = this_.d;
        _temp_1[int32_t {0}] = _temp_2;
        _temp_1[int32_t {1}] = _temp_2;
        highValue = ((x - (_temp * this_.z[int32_t {0}])) - this_.z[int32_t {1}]) * _temp_1;
        _temp_4 = this_.a;
        _temp_3[int32_t {0}] = _temp_4;
        _temp_3[int32_t {1}] = _temp_4;
        bandValue = (_temp_3 * highValue) + this_.z[int32_t {0}];
        _temp_6 = this_.a;
        _temp_5[int32_t {0}] = _temp_6;
        _temp_5[int32_t {1}] = _temp_6;
        lowValue = (_temp_5 * bandValue) + this_.z[int32_t {1}];
        _temp_8 = this_.a;
        _temp_7[int32_t {0}] = _temp_8;
        _temp_7[int32_t {1}] = _temp_8;
        this_.z[int32_t {0}] = ((_temp_7 * highValue) + bandValue);
        _temp_10 = this_.a;
        _temp_9[int32_t {0}] = _temp_10;
        _temp_9[int32_t {1}] = _temp_10;
        this_.z[int32_t {1}] = ((_temp_9 * bandValue) + lowValue);
        _temp_11[int32_t {0}] = lowValue;
        _temp_11[int32_t {1}] = highValue;
        _temp_11[int32_t {2}] = bandValue;
        return _temp_11;
    }

    //==============================================================================
    const char* getStringForHandle (uint32_t handle, size_t& stringLength)
    {
        (void) handle; (void) stringLength;
        return "";
    }

    //==============================================================================
    int32_t g__sessionID {};
    double g__frequency {};
    static constexpr float g_initialFrequency { 1000.0f };
    static constexpr float g_initialQ { 0.707107f };
    static constexpr int32_t g_initialMode { int32_t {0} };
    static constexpr int32_t g_filterMode { int32_t {0} };
    static constexpr int32_t g_framesPerParameterUpdate { int32_t {32} };
    static constexpr int32_t g_lowPass { int32_t {0} };
    static constexpr int32_t g_highPass { int32_t {1} };

    //==============================================================================
    #if __clang__
     #pragma clang diagnostic pop
    #elif __GNUC__
     #pragma GCC diagnostic pop
    #else
     #pragma warning (pop)
    #endif
};

