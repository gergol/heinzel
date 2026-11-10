
#include <cstdint>
#include <cmath>
#include <string>
#include <cstring>
#include <array>
#include <stdexcept>
#include <type_traits>
#include <limits>

//==============================================================================
/// Auto-generated C++ class for the 'Freeverb' processor
///

#if ! (defined (__cplusplus) && (__cplusplus >= 201703L))
 #error "This code requires that your compiler is set to use C++17 or later!"
#endif

struct Freeverb
{
    Freeverb() = default;
    ~Freeverb() = default;

    static constexpr std::string_view name = "Freeverb";

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
    static constexpr uint32_t numInputEndpoints  = 6;
    static constexpr uint32_t numOutputEndpoints = 1;

    static constexpr uint32_t maxFramesPerBlock  = 512;
    static constexpr uint32_t eventBufferSize    = 32;
    static constexpr uint32_t maxOutputEventSize = 0;
    static constexpr double   latency            = 0.000000;

    enum class EndpointHandles
    {
        audioIn  = 1,
        audioOut = 7,
        roomSize = 2,
        damping  = 3,
        width    = 4,
        wetLevel = 5,
        dryLevel = 6
    };

    static constexpr uint32_t getEndpointHandleForName (std::string_view endpointName)
    {
        if (endpointName == "audioIn")   return static_cast<uint32_t> (EndpointHandles::audioIn);
        if (endpointName == "audioOut")  return static_cast<uint32_t> (EndpointHandles::audioOut);
        if (endpointName == "roomSize")  return static_cast<uint32_t> (EndpointHandles::roomSize);
        if (endpointName == "damping")   return static_cast<uint32_t> (EndpointHandles::damping);
        if (endpointName == "width")     return static_cast<uint32_t> (EndpointHandles::width);
        if (endpointName == "wetLevel")  return static_cast<uint32_t> (EndpointHandles::wetLevel);
        if (endpointName == "dryLevel")  return static_cast<uint32_t> (EndpointHandles::dryLevel);
        return 0;
    }

    static constexpr EndpointInfo inputEndpoints[] =
    {
        { 1,  "audioIn",   EndpointType::stream },
        { 2,  "roomSize",  EndpointType::event  },
        { 3,  "damping",   EndpointType::event  },
        { 4,  "width",     EndpointType::event  },
        { 5,  "wetLevel",  EndpointType::event  },
        { 6,  "dryLevel",  EndpointType::event  }
    };

    static constexpr EndpointInfo outputEndpoints[] =
    {
        { 7,  "audioOut",  EndpointType::stream }
    };

    //==============================================================================
    static constexpr uint32_t numAudioInputChannels  = 1;
    static constexpr uint32_t numAudioOutputChannels = 2;

    static constexpr std::array outputAudioStreams
    {
        outputEndpoints[0]
    };

    static constexpr std::array<EndpointInfo, 0> outputEvents {};

    static constexpr std::array<EndpointInfo, 0> outputMIDIEvents {};

    static constexpr std::array inputAudioStreams
    {
        inputEndpoints[0]
    };

    static constexpr std::array inputEvents
    {
        inputEndpoints[1],
        inputEndpoints[2],
        inputEndpoints[3],
        inputEndpoints[4],
        inputEndpoints[5]
    };

    static constexpr std::array<EndpointInfo, 0> inputMIDIEvents {};

    static constexpr std::array inputParameters
    {
        inputEndpoints[1],
        inputEndpoints[2],
        inputEndpoints[3],
        inputEndpoints[4],
        inputEndpoints[5]
    };

    static constexpr const char* programDetailsJSON =
            "{\n"
            "  \"mainProcessor\": \"Freeverb\",\n"
            "  \"inputs\": [\n"
            "    {\n"
            "      \"endpointID\": \"audioIn\",\n"
            "      \"endpointType\": \"stream\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"purpose\": \"audio in\",\n"
            "      \"numAudioChannels\": 1\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"roomSize\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Room Size\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 100,\n"
            "        \"init\": 80,\n"
            "        \"text\": \"Tiny|Small|Medium|Large|Hall\"\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"damping\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Damping Factor\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 100,\n"
            "        \"init\": 50,\n"
            "        \"unit\": \"%\",\n"
            "        \"step\": 1\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"width\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Width\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 100,\n"
            "        \"init\": 100,\n"
            "        \"unit\": \"%\",\n"
            "        \"step\": 1\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"wetLevel\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Wet Level\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 100,\n"
            "        \"init\": 33,\n"
            "        \"unit\": \"%\",\n"
            "        \"step\": 1\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"dryLevel\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Dry Level\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 100,\n"
            "        \"init\": 40,\n"
            "        \"unit\": \"%\",\n"
            "        \"step\": 1\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    }\n"
            "  ],\n"
            "  \"outputs\": [\n"
            "    {\n"
            "      \"endpointID\": \"audioOut\",\n"
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
    struct _ParameterScaler_1_State
    {
    };

    using std_std_smoothing_specialised_f32_L7NXOd_FrameType = float;
    struct std_std_smoothing_specialised_f32_L7NXOd_SmoothedValue
    {
        float value = {};
        float target = {};
        float increment = {};
        int32_t remainingSteps = {};
    };

    struct std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State
    {
        std_std_smoothing_specialised_f32_L7NXOd_SmoothedValue smoothedValue;
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1116_1_State
    {
        Array<float, 1116> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1188_1_State
    {
        Array<float, 1188> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1277_1_State
    {
        Array<float, 1277> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1356_1_State
    {
        Array<float, 1356> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1422_1_State
    {
        Array<float, 1422> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1491_1_State
    {
        Array<float, 1491> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1557_1_State
    {
        Array<float, 1557> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1617_1_State
    {
        Array<float, 1617> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _AllpassFilter_specialised_f32_225_1_State
    {
        Array<float, 225> buffer;
        int32_t index = {};
        int32_t _resumeIndex = {};
    };

    struct _AllpassFilter_specialised_f32_341_1_State
    {
        Array<float, 341> buffer;
        int32_t index = {};
        int32_t _resumeIndex = {};
    };

    struct _AllpassFilter_specialised_f32_441_1_State
    {
        Array<float, 441> buffer;
        int32_t index = {};
        int32_t _resumeIndex = {};
    };

    struct _AllpassFilter_specialised_f32_556_1_State
    {
        Array<float, 556> buffer;
        int32_t index = {};
        int32_t _resumeIndex = {};
    };

    struct _MonoReverb_specialised_0_1_State
    {
        _CombFilter_specialised_f32_1116_1_State comb1;
        _CombFilter_specialised_f32_1188_1_State comb2;
        _CombFilter_specialised_f32_1277_1_State comb3;
        _CombFilter_specialised_f32_1356_1_State comb4;
        _CombFilter_specialised_f32_1422_1_State comb5;
        _CombFilter_specialised_f32_1491_1_State comb6;
        _CombFilter_specialised_f32_1557_1_State comb7;
        _CombFilter_specialised_f32_1617_1_State comb8;
        _AllpassFilter_specialised_f32_225_1_State allpass1;
        _AllpassFilter_specialised_f32_341_1_State allpass2;
        _AllpassFilter_specialised_f32_441_1_State allpass3;
        _AllpassFilter_specialised_f32_556_1_State allpass4;
    };

    struct _CombFilter_specialised_f32_1139_1_State
    {
        Array<float, 1139> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1211_1_State
    {
        Array<float, 1211> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1300_1_State
    {
        Array<float, 1300> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1379_1_State
    {
        Array<float, 1379> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1445_1_State
    {
        Array<float, 1445> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1514_1_State
    {
        Array<float, 1514> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1580_1_State
    {
        Array<float, 1580> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _CombFilter_specialised_f32_1640_1_State
    {
        Array<float, 1640> buffer;
        int32_t index = {};
        float last = {};
        int32_t _resumeIndex = {};
    };

    struct _AllpassFilter_specialised_f32_248_1_State
    {
        Array<float, 248> buffer;
        int32_t index = {};
        int32_t _resumeIndex = {};
    };

    struct _AllpassFilter_specialised_f32_364_1_State
    {
        Array<float, 364> buffer;
        int32_t index = {};
        int32_t _resumeIndex = {};
    };

    struct _AllpassFilter_specialised_f32_464_1_State
    {
        Array<float, 464> buffer;
        int32_t index = {};
        int32_t _resumeIndex = {};
    };

    struct _AllpassFilter_specialised_f32_579_1_State
    {
        Array<float, 579> buffer;
        int32_t index = {};
        int32_t _resumeIndex = {};
    };

    struct _MonoReverb_specialised_23_1_State
    {
        _CombFilter_specialised_f32_1139_1_State comb1;
        _CombFilter_specialised_f32_1211_1_State comb2;
        _CombFilter_specialised_f32_1300_1_State comb3;
        _CombFilter_specialised_f32_1379_1_State comb4;
        _CombFilter_specialised_f32_1445_1_State comb5;
        _CombFilter_specialised_f32_1514_1_State comb6;
        _CombFilter_specialised_f32_1580_1_State comb7;
        _CombFilter_specialised_f32_1640_1_State comb8;
        _AllpassFilter_specialised_f32_248_1_State allpass1;
        _AllpassFilter_specialised_f32_364_1_State allpass2;
        _AllpassFilter_specialised_f32_464_1_State allpass3;
        _AllpassFilter_specialised_f32_579_1_State allpass4;
    };

    struct _WetDryMixer_1_State
    {
        int32_t _resumeIndex = {};
    };

    struct _Freeverb_State
    {
        int32_t _sessionID = {};
        double _frequency = {};
        _ParameterScaler_1_State parameterScaler;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State wetGainSmoother;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State dryGainSmoother;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State widthSmoother;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State dampingSmoother;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State feedbackSmoother;
        _MonoReverb_specialised_0_1_State reverbL;
        _MonoReverb_specialised_23_1_State reverbR;
        _WetDryMixer_1_State mixer;
    };

    struct Freeverb_State
    {
        int32_t _currentFrame = {};
        _Freeverb_State _state;
    };

    struct Freeverb_IO
    {
        Array<float, 512> audioIn;
        Array<Vector<float, 2>, 512> audioOut;
    };

    struct _Freeverb_IO
    {
        float audioIn = {};
        Vector<float, 2> audioOut;
    };

    struct _ParameterScaler_1_IO
    {
    };

    struct std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO
    {
        float out = {};
    };

    struct _MonoReverb_specialised_0_1_IO
    {
        float audioIn = {};
        float damping = {};
        float feedback = {};
        float audioOut = {};
    };

    struct _WetDryMixer_1_IO
    {
        Vector<float, 2> out;
        float audioInDry = {};
        float audioInWetL = {};
        float audioInWetR = {};
        float width = {};
        float wetGain = {};
        float dryGain = {};
    };

    struct _CombFilter_specialised_f32_1116_1_IO
    {
        float in = {};
        float out = {};
        float damping = {};
        float feedback = {};
    };

    struct _AllpassFilter_specialised_f32_225_1_IO
    {
        float in = {};
        float out = {};
    };

    using std_intrinsics_T = int32_t;

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
        cmajIO.audioOut.clear (static_cast<SizeType> (frames));
        _advance (cmajState, cmajIO, frames);
    }

    void copyOutputValue (EndpointHandle endpointHandle, void* dest)
    {
        (void) endpointHandle; (void) dest;

        throw std::runtime_error ("Unknown value endpointHandle:" + std::to_string (endpointHandle));
    }

    void copyOutputFrames (EndpointHandle endpointHandle, void* dest, uint32_t numFramesToCopy)
    {
        if (endpointHandle == 7) { std::memcpy (reinterpret_cast<char*> (dest), std::addressof (cmajIO.audioOut), 8 * numFramesToCopy); std::memset (reinterpret_cast<char*> (std::addressof (cmajIO.audioOut)), 0, 8 * numFramesToCopy); return; }
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

    void addEvent_roomSize (float event)
    {
        _sendEvent_roomSize (cmajState, event);
    }

    void addEvent_damping (float event)
    {
        _sendEvent_damping (cmajState, event);
    }

    void addEvent_width (float event)
    {
        _sendEvent_width (cmajState, event);
    }

    void addEvent_wetLevel (float event)
    {
        _sendEvent_wetLevel (cmajState, event);
    }

    void addEvent_dryLevel (float event)
    {
        _sendEvent_dryLevel (cmajState, event);
    }

    void addEvent (EndpointHandle endpointHandle, uint32_t typeIndex, const unsigned char* eventData)
    {
        (void) endpointHandle; (void) typeIndex; (void) eventData;

        if (endpointHandle == 2)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_roomSize (value);
        }

        if (endpointHandle == 3)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_damping (value);
        }

        if (endpointHandle == 4)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_width (value);
        }

        if (endpointHandle == 5)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_wetLevel (value);
        }

        if (endpointHandle == 6)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_dryLevel (value);
        }
    }

    void setValue (EndpointHandle endpointHandle, const void* value, int32_t frames)
    {
        (void) endpointHandle; (void) value; (void) frames;
    }

    void setInputFrames_audioIn (const void* data, uint32_t numFrames, uint32_t numTrailingFramesToClear)
    {
        memcpy (cmajIO.audioIn.elements, data, numFrames * 4);
        if (numTrailingFramesToClear != 0) memset (reinterpret_cast<char*> (cmajIO.audioIn.elements + numFrames), 0, numTrailingFramesToClear * 4);
    }

    void setInputFrames (EndpointHandle endpointHandle, const void* frameData, uint32_t numFrames, uint32_t numTrailingFramesToClear)
    {
        if (endpointHandle == 1) return setInputFrames_audioIn (frameData, numFrames, numTrailingFramesToClear);
    }

    //==============================================================================
    // Rendering state values
    int32_t initSessionID;
    double initFrequency;
    Freeverb_State cmajState = {};
    Freeverb_IO cmajIO = {};

    //==============================================================================
    void _sendEvent_roomSize (Freeverb_State& _state, float value) noexcept
    {
        _Freeverb__roomSize (_state._state, value);
    }

    void _Freeverb__roomSize (_Freeverb_State& _state, float value) noexcept
    {
        ParameterScaler__roomSize (_state.parameterScaler, value);
    }

    void ParameterScaler__roomSize (_ParameterScaler_1_State& _state, float newValue) noexcept
    {
        ParameterScaler___writeEvent_feedbackOut (_state, ((newValue * g_roomScaleFactor) / 100.0f) + g_roomOffset);
    }

    void ParameterScaler___writeEvent_feedbackOut (_ParameterScaler_1_State& _state, float value) noexcept
    {
        _Freeverb___forwardEvent_5 (state_upcast_struct_Freeverb_State_st_YC9hVb(_state), value);
    }

    void _Freeverb___forwardEvent_5 (_Freeverb_State& _state, float value) noexcept
    {
        std__smoothing__SmoothedValueStream__in (_state.feedbackSmoother, value);
    }

    void std__smoothing__SmoothedValueStream__in (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, float newTarget) noexcept
    {
        std__smoothing__setTarget (_state.smoothedValue, newTarget, intrinsics::max (int32_t {1}, static_cast<int32_t> (g_smoothingTimeSeconds / static_cast<float> (1.0 / (1.0 * g__frequency)))));
    }

    void std__smoothing__setTarget (std_std_smoothing_specialised_f32_L7NXOd_SmoothedValue& this_, float targetValue, int32_t stepsToReachTarget) noexcept
    {
        float  _temp;
        float  _temp_0;

        stepsToReachTarget = intrinsics::max (int32_t {1}, stepsToReachTarget);
        this_.target = targetValue;
        _temp = targetValue;
        _temp_0 = _temp - std__smoothing__currentValue (this_);
        this_.increment = (_temp_0 / static_cast<float> (stepsToReachTarget));
        this_.remainingSteps = stepsToReachTarget;
        this_.value = targetValue;
    }

    int32_t std__intrinsics__max (int32_t v1, int32_t v2) noexcept
    {
        {
            return (v1 > v2) ? v1 : v2;
        }
    }

    float std__smoothing__currentValue (std_std_smoothing_specialised_f32_L7NXOd_SmoothedValue& this_) noexcept
    {
        return this_.value - (this_.increment * static_cast<float> (this_.remainingSteps));
    }

    void _sendEvent_damping (Freeverb_State& _state, float value) noexcept
    {
        _Freeverb__damping (_state._state, value);
    }

    void _Freeverb__damping (_Freeverb_State& _state, float value) noexcept
    {
        ParameterScaler__damping (_state.parameterScaler, value);
    }

    void ParameterScaler__damping (_ParameterScaler_1_State& _state, float newValue) noexcept
    {
        ParameterScaler___writeEvent_dampingOut (_state, (newValue * g_dampScaleFactor) / 100.0f);
    }

    void ParameterScaler___writeEvent_dampingOut (_ParameterScaler_1_State& _state, float value) noexcept
    {
        _Freeverb___forwardEvent_4 (state_upcast_struct_Freeverb_State_st_YC9hVb(_state), value);
    }

    void _Freeverb___forwardEvent_4 (_Freeverb_State& _state, float value) noexcept
    {
        std__smoothing__SmoothedValueStream__in_0 (_state.dampingSmoother, value);
    }

    void std__smoothing__SmoothedValueStream__in_0 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, float newTarget) noexcept
    {
        std__smoothing__setTarget (_state.smoothedValue, newTarget, intrinsics::max (int32_t {1}, static_cast<int32_t> (g_smoothingTimeSeconds_0 / static_cast<float> (1.0 / (1.0 * g__frequency)))));
    }

    void _sendEvent_width (Freeverb_State& _state, float value) noexcept
    {
        _Freeverb__width (_state._state, value);
    }

    void _Freeverb__width (_Freeverb_State& _state, float value) noexcept
    {
        ParameterScaler__width (_state.parameterScaler, value);
    }

    void ParameterScaler__width (_ParameterScaler_1_State& _state, float newValue) noexcept
    {
        ParameterScaler___writeEvent_widthOut (_state, newValue / 100.0f);
    }

    void ParameterScaler___writeEvent_widthOut (_ParameterScaler_1_State& _state, float value) noexcept
    {
        _Freeverb___forwardEvent_3 (state_upcast_struct_Freeverb_State_st_YC9hVb(_state), value);
    }

    void _Freeverb___forwardEvent_3 (_Freeverb_State& _state, float value) noexcept
    {
        std__smoothing__SmoothedValueStream__in_1 (_state.widthSmoother, value);
    }

    void std__smoothing__SmoothedValueStream__in_1 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, float newTarget) noexcept
    {
        std__smoothing__setTarget (_state.smoothedValue, newTarget, intrinsics::max (int32_t {1}, static_cast<int32_t> (g_smoothingTimeSeconds_1 / static_cast<float> (1.0 / (1.0 * g__frequency)))));
    }

    void _sendEvent_wetLevel (Freeverb_State& _state, float value) noexcept
    {
        _Freeverb__wetLevel (_state._state, value);
    }

    void _Freeverb__wetLevel (_Freeverb_State& _state, float value) noexcept
    {
        ParameterScaler__wetLevel (_state.parameterScaler, value);
    }

    void ParameterScaler__wetLevel (_ParameterScaler_1_State& _state, float newValue) noexcept
    {
        ParameterScaler___writeEvent_wetGainOut (_state, (newValue * g_wetScaleFactor) / 100.0f);
    }

    void ParameterScaler___writeEvent_wetGainOut (_ParameterScaler_1_State& _state, float value) noexcept
    {
        _Freeverb___forwardEvent (state_upcast_struct_Freeverb_State_st_YC9hVb(_state), value);
    }

    void _Freeverb___forwardEvent (_Freeverb_State& _state, float value) noexcept
    {
        std__smoothing__SmoothedValueStream__in_2 (_state.wetGainSmoother, value);
    }

    void std__smoothing__SmoothedValueStream__in_2 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, float newTarget) noexcept
    {
        std__smoothing__setTarget (_state.smoothedValue, newTarget, intrinsics::max (int32_t {1}, static_cast<int32_t> (g_smoothingTimeSeconds_2 / static_cast<float> (1.0 / (1.0 * g__frequency)))));
    }

    void _sendEvent_dryLevel (Freeverb_State& _state, float value) noexcept
    {
        _Freeverb__dryLevel (_state._state, value);
    }

    void _Freeverb__dryLevel (_Freeverb_State& _state, float value) noexcept
    {
        ParameterScaler__dryLevel (_state.parameterScaler, value);
    }

    void ParameterScaler__dryLevel (_ParameterScaler_1_State& _state, float newValue) noexcept
    {
        ParameterScaler___writeEvent_dryGainOut (_state, (newValue * g_dryScaleFactor) / 100.0f);
    }

    void ParameterScaler___writeEvent_dryGainOut (_ParameterScaler_1_State& _state, float value) noexcept
    {
        _Freeverb___forwardEvent_2 (state_upcast_struct_Freeverb_State_st_YC9hVb(_state), value);
    }

    void _Freeverb___forwardEvent_2 (_Freeverb_State& _state, float value) noexcept
    {
        std__smoothing__SmoothedValueStream__in_3 (_state.dryGainSmoother, value);
    }

    void std__smoothing__SmoothedValueStream__in_3 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, float newTarget) noexcept
    {
        std__smoothing__setTarget (_state.smoothedValue, newTarget, intrinsics::max (int32_t {1}, static_cast<int32_t> (g_smoothingTimeSeconds_3 / static_cast<float> (1.0 / (1.0 * g__frequency)))));
    }

    void _initialise (Freeverb_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        _Freeverb___initialise (_state._state, processorID, sessionID, frequency);
    }

    void _Freeverb___initialise (_Freeverb_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        _initialise_0 (_state.parameterScaler, processorID, sessionID, frequency);
        std__smoothing__SmoothedValueStream___initialise (_state.wetGainSmoother, processorID, sessionID, frequency);
        std__smoothing__SmoothedValueStream___initialise_0 (_state.dryGainSmoother, processorID, sessionID, frequency);
        std__smoothing__SmoothedValueStream___initialise_1 (_state.widthSmoother, processorID, sessionID, frequency);
        std__smoothing__SmoothedValueStream___initialise_2 (_state.dampingSmoother, processorID, sessionID, frequency);
        std__smoothing__SmoothedValueStream___initialise_3 (_state.feedbackSmoother, processorID, sessionID, frequency);
        _initialise_1 (_state.reverbL, processorID, sessionID, frequency);
        _initialise_2 (_state.reverbR, processorID, sessionID, frequency);
        _initialise_3 (_state.mixer, processorID, sessionID, frequency);
    }

    void _initialise_0 (_ParameterScaler_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void std__smoothing__SmoothedValueStream___initialise (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        std__smoothing__SmoothedValueStream__init (_state);
    }

    void std__smoothing__SmoothedValueStream__init (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state) noexcept
    {
        std__smoothing__reset (_state.smoothedValue, g_initialValue);
    }

    void std__smoothing__reset (std_std_smoothing_specialised_f32_L7NXOd_SmoothedValue& this_, float newValue) noexcept
    {
        this_.value = newValue;
        this_.target = newValue;
        this_.increment = 0.0f;
        this_.remainingSteps = int32_t {0};
    }

    void std__smoothing__SmoothedValueStream___initialise_0 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        std__smoothing__SmoothedValueStream__init_0 (_state);
    }

    void std__smoothing__SmoothedValueStream__init_0 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state) noexcept
    {
        std__smoothing__reset (_state.smoothedValue, g_initialValue_0);
    }

    void std__smoothing__SmoothedValueStream___initialise_1 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        std__smoothing__SmoothedValueStream__init_1 (_state);
    }

    void std__smoothing__SmoothedValueStream__init_1 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state) noexcept
    {
        std__smoothing__reset (_state.smoothedValue, g_initialValue_1);
    }

    void std__smoothing__SmoothedValueStream___initialise_2 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        std__smoothing__SmoothedValueStream__init_2 (_state);
    }

    void std__smoothing__SmoothedValueStream__init_2 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state) noexcept
    {
        std__smoothing__reset (_state.smoothedValue, g_initialValue_2);
    }

    void std__smoothing__SmoothedValueStream___initialise_3 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        std__smoothing__SmoothedValueStream__init_3 (_state);
    }

    void std__smoothing__SmoothedValueStream__init_3 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state) noexcept
    {
        std__smoothing__reset (_state.smoothedValue, g_initialValue_3);
    }

    void _initialise_1 (_MonoReverb_specialised_0_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        _initialise_4 (_state.comb1, processorID, sessionID, frequency);
        _initialise_5 (_state.comb2, processorID, sessionID, frequency);
        _initialise_6 (_state.comb3, processorID, sessionID, frequency);
        _initialise_7 (_state.comb4, processorID, sessionID, frequency);
        _initialise_8 (_state.comb5, processorID, sessionID, frequency);
        _initialise_9 (_state.comb6, processorID, sessionID, frequency);
        _initialise_10 (_state.comb7, processorID, sessionID, frequency);
        _initialise_11 (_state.comb8, processorID, sessionID, frequency);
        _initialise_12 (_state.allpass1, processorID, sessionID, frequency);
        _initialise_13 (_state.allpass2, processorID, sessionID, frequency);
        _initialise_14 (_state.allpass3, processorID, sessionID, frequency);
        _initialise_15 (_state.allpass4, processorID, sessionID, frequency);
    }

    void _initialise_4 (_CombFilter_specialised_f32_1116_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_5 (_CombFilter_specialised_f32_1188_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_6 (_CombFilter_specialised_f32_1277_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_7 (_CombFilter_specialised_f32_1356_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_8 (_CombFilter_specialised_f32_1422_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_9 (_CombFilter_specialised_f32_1491_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_10 (_CombFilter_specialised_f32_1557_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_11 (_CombFilter_specialised_f32_1617_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_12 (_AllpassFilter_specialised_f32_225_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_13 (_AllpassFilter_specialised_f32_341_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_14 (_AllpassFilter_specialised_f32_441_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_15 (_AllpassFilter_specialised_f32_556_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_2 (_MonoReverb_specialised_23_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        _initialise_16 (_state.comb1, processorID, sessionID, frequency);
        _initialise_17 (_state.comb2, processorID, sessionID, frequency);
        _initialise_18 (_state.comb3, processorID, sessionID, frequency);
        _initialise_19 (_state.comb4, processorID, sessionID, frequency);
        _initialise_20 (_state.comb5, processorID, sessionID, frequency);
        _initialise_21 (_state.comb6, processorID, sessionID, frequency);
        _initialise_22 (_state.comb7, processorID, sessionID, frequency);
        _initialise_23 (_state.comb8, processorID, sessionID, frequency);
        _initialise_24 (_state.allpass1, processorID, sessionID, frequency);
        _initialise_25 (_state.allpass2, processorID, sessionID, frequency);
        _initialise_26 (_state.allpass3, processorID, sessionID, frequency);
        _initialise_27 (_state.allpass4, processorID, sessionID, frequency);
    }

    void _initialise_16 (_CombFilter_specialised_f32_1139_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_17 (_CombFilter_specialised_f32_1211_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_18 (_CombFilter_specialised_f32_1300_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_19 (_CombFilter_specialised_f32_1379_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_20 (_CombFilter_specialised_f32_1445_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_21 (_CombFilter_specialised_f32_1514_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_22 (_CombFilter_specialised_f32_1580_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_23 (_CombFilter_specialised_f32_1640_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_24 (_AllpassFilter_specialised_f32_248_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_25 (_AllpassFilter_specialised_f32_364_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_26 (_AllpassFilter_specialised_f32_464_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_27 (_AllpassFilter_specialised_f32_579_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _initialise_3 (_WetDryMixer_1_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
    }

    void _advance (Freeverb_State& _state, Freeverb_IO& _io, int32_t _frames) noexcept
    {
        _Freeverb_IO  ioCopy;

        for (;;)
        {
            if (_state._currentFrame == _frames)
            {
                break;
            }
            ioCopy = _Freeverb_IO {};
            ioCopy.audioIn = _io.audioIn[_state._currentFrame];
            main (_state._state, ioCopy);
            _io.audioOut[_state._currentFrame] = ioCopy.audioOut;
            intrinsics::addTo (_state._currentFrame, 1);
        }
        _state._currentFrame = int32_t {0};
    }

    void main (_Freeverb_State& _state, _Freeverb_IO& _io) noexcept
    {
        _ParameterScaler_1_IO  parameterScaler_io;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO  wetGainSmoother_io;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO  dryGainSmoother_io;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO  widthSmoother_io;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO  dampingSmoother_io;
        std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO  feedbackSmoother_io;
        _MonoReverb_specialised_0_1_IO  reverbL_io;
        _MonoReverb_specialised_0_1_IO  reverbR_io;
        _WetDryMixer_1_IO  mixer_io;

        parameterScaler_io = _ParameterScaler_1_IO {};
        wetGainSmoother_io = std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO {};
        dryGainSmoother_io = std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO {};
        widthSmoother_io = std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO {};
        dampingSmoother_io = std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO {};
        feedbackSmoother_io = std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO {};
        reverbL_io = _MonoReverb_specialised_0_1_IO {};
        reverbR_io = _MonoReverb_specialised_0_1_IO {};
        mixer_io = _WetDryMixer_1_IO {};
        {
        }
        main_0 (_state.parameterScaler, parameterScaler_io);
        {
        }
        std__smoothing__SmoothedValueStream__main (_state.wetGainSmoother, wetGainSmoother_io);
        {
        }
        std__smoothing__SmoothedValueStream__main_0 (_state.dryGainSmoother, dryGainSmoother_io);
        {
        }
        std__smoothing__SmoothedValueStream__main_1 (_state.widthSmoother, widthSmoother_io);
        {
        }
        std__smoothing__SmoothedValueStream__main_2 (_state.dampingSmoother, dampingSmoother_io);
        {
        }
        std__smoothing__SmoothedValueStream__main_3 (_state.feedbackSmoother, feedbackSmoother_io);
        {
            reverbL_io.audioIn = (reverbL_io.audioIn + _io.audioIn);
            reverbL_io.damping = (reverbL_io.damping + dampingSmoother_io.out);
            reverbL_io.feedback = (reverbL_io.feedback + feedbackSmoother_io.out);
        }
        main_1 (_state.reverbL, reverbL_io);
        {
            reverbR_io.audioIn = (reverbR_io.audioIn + _io.audioIn);
            reverbR_io.damping = (reverbR_io.damping + dampingSmoother_io.out);
            reverbR_io.feedback = (reverbR_io.feedback + feedbackSmoother_io.out);
        }
        main_2 (_state.reverbR, reverbR_io);
        {
            mixer_io.audioInDry = (mixer_io.audioInDry + _io.audioIn);
            mixer_io.audioInWetL = (mixer_io.audioInWetL + reverbL_io.audioOut);
            mixer_io.audioInWetR = (mixer_io.audioInWetR + reverbR_io.audioOut);
            mixer_io.wetGain = (mixer_io.wetGain + wetGainSmoother_io.out);
            mixer_io.dryGain = (mixer_io.dryGain + dryGainSmoother_io.out);
            mixer_io.width = (mixer_io.width + widthSmoother_io.out);
        }
        WetDryMixer__main (_state.mixer, mixer_io);
        {
            _io.audioOut = (_io.audioOut + mixer_io.out);
        }
    }

    void main_0 (_ParameterScaler_1_State& _state, _ParameterScaler_1_IO& _io) noexcept
    {
    }

    void std__smoothing__SmoothedValueStream__main (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO& _io) noexcept
    {
        float  _temp;

        for (;;)
        {
            _temp = _io.out;
            _io.out = (_temp + std__smoothing__getNext (_state.smoothedValue));
            return;
        }
    }

    float std__smoothing__getNext (std_std_smoothing_specialised_f32_L7NXOd_SmoothedValue& this_) noexcept
    {
        if (this_.remainingSteps > int32_t {0})
        {
            intrinsics::addTo (this_.remainingSteps, -1);
            return std__smoothing__currentValue (this_);
        }
        return this_.value;
    }

    void std__smoothing__SmoothedValueStream__main_0 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO& _io) noexcept
    {
        float  _temp;

        for (;;)
        {
            _temp = _io.out;
            _io.out = (_temp + std__smoothing__getNext (_state.smoothedValue));
            return;
        }
    }

    void std__smoothing__SmoothedValueStream__main_1 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO& _io) noexcept
    {
        float  _temp;

        for (;;)
        {
            _temp = _io.out;
            _io.out = (_temp + std__smoothing__getNext (_state.smoothedValue));
            return;
        }
    }

    void std__smoothing__SmoothedValueStream__main_2 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO& _io) noexcept
    {
        float  _temp;

        for (;;)
        {
            _temp = _io.out;
            _io.out = (_temp + std__smoothing__getNext (_state.smoothedValue));
            return;
        }
    }

    void std__smoothing__SmoothedValueStream__main_3 (std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_State& _state, std_std_smoothing_specialised_f32_L7NXOd_std_std_smoothing_specialised_f32_L7NXO_Y0vV9b_1_IO& _io) noexcept
    {
        float  _temp;

        for (;;)
        {
            _temp = _io.out;
            _io.out = (_temp + std__smoothing__getNext (_state.smoothedValue));
            return;
        }
    }

    void main_1 (_MonoReverb_specialised_0_1_State& _state, _MonoReverb_specialised_0_1_IO& _io) noexcept
    {
        _CombFilter_specialised_f32_1116_1_IO  comb1_io;
        _CombFilter_specialised_f32_1116_1_IO  comb2_io;
        _CombFilter_specialised_f32_1116_1_IO  comb3_io;
        _CombFilter_specialised_f32_1116_1_IO  comb4_io;
        _CombFilter_specialised_f32_1116_1_IO  comb5_io;
        _CombFilter_specialised_f32_1116_1_IO  comb6_io;
        _CombFilter_specialised_f32_1116_1_IO  comb7_io;
        _CombFilter_specialised_f32_1116_1_IO  comb8_io;
        _AllpassFilter_specialised_f32_225_1_IO  allpass1_io;
        _AllpassFilter_specialised_f32_225_1_IO  allpass2_io;
        _AllpassFilter_specialised_f32_225_1_IO  allpass3_io;
        _AllpassFilter_specialised_f32_225_1_IO  allpass4_io;

        comb1_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb2_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb3_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb4_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb5_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb6_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb7_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb8_io = _CombFilter_specialised_f32_1116_1_IO {};
        allpass1_io = _AllpassFilter_specialised_f32_225_1_IO {};
        allpass2_io = _AllpassFilter_specialised_f32_225_1_IO {};
        allpass3_io = _AllpassFilter_specialised_f32_225_1_IO {};
        allpass4_io = _AllpassFilter_specialised_f32_225_1_IO {};
        {
            comb1_io.in = (comb1_io.in + _io.audioIn);
            comb1_io.damping = (comb1_io.damping + _io.damping);
            comb1_io.feedback = (comb1_io.feedback + _io.feedback);
        }
        CombFilter__main (_state.comb1, comb1_io);
        {
            comb2_io.in = (comb2_io.in + _io.audioIn);
            comb2_io.damping = (comb2_io.damping + _io.damping);
            comb2_io.feedback = (comb2_io.feedback + _io.feedback);
        }
        CombFilter__main_0 (_state.comb2, comb2_io);
        {
            comb3_io.in = (comb3_io.in + _io.audioIn);
            comb3_io.damping = (comb3_io.damping + _io.damping);
            comb3_io.feedback = (comb3_io.feedback + _io.feedback);
        }
        CombFilter__main_1 (_state.comb3, comb3_io);
        {
            comb4_io.in = (comb4_io.in + _io.audioIn);
            comb4_io.damping = (comb4_io.damping + _io.damping);
            comb4_io.feedback = (comb4_io.feedback + _io.feedback);
        }
        CombFilter__main_2 (_state.comb4, comb4_io);
        {
            comb5_io.in = (comb5_io.in + _io.audioIn);
            comb5_io.damping = (comb5_io.damping + _io.damping);
            comb5_io.feedback = (comb5_io.feedback + _io.feedback);
        }
        CombFilter__main_3 (_state.comb5, comb5_io);
        {
            comb6_io.in = (comb6_io.in + _io.audioIn);
            comb6_io.damping = (comb6_io.damping + _io.damping);
            comb6_io.feedback = (comb6_io.feedback + _io.feedback);
        }
        CombFilter__main_4 (_state.comb6, comb6_io);
        {
            comb7_io.in = (comb7_io.in + _io.audioIn);
            comb7_io.damping = (comb7_io.damping + _io.damping);
            comb7_io.feedback = (comb7_io.feedback + _io.feedback);
        }
        CombFilter__main_5 (_state.comb7, comb7_io);
        {
            comb8_io.in = (comb8_io.in + _io.audioIn);
            comb8_io.damping = (comb8_io.damping + _io.damping);
            comb8_io.feedback = (comb8_io.feedback + _io.feedback);
        }
        CombFilter__main_6 (_state.comb8, comb8_io);
        {
            allpass1_io.in = (allpass1_io.in + comb1_io.out);
            allpass1_io.in = (allpass1_io.in + comb2_io.out);
            allpass1_io.in = (allpass1_io.in + comb3_io.out);
            allpass1_io.in = (allpass1_io.in + comb4_io.out);
            allpass1_io.in = (allpass1_io.in + comb5_io.out);
            allpass1_io.in = (allpass1_io.in + comb6_io.out);
            allpass1_io.in = (allpass1_io.in + comb7_io.out);
            allpass1_io.in = (allpass1_io.in + comb8_io.out);
        }
        AllpassFilter__main (_state.allpass1, allpass1_io);
        {
            allpass2_io.in = (allpass2_io.in + allpass1_io.out);
        }
        AllpassFilter__main_0 (_state.allpass2, allpass2_io);
        {
            allpass3_io.in = (allpass3_io.in + allpass2_io.out);
        }
        AllpassFilter__main_1 (_state.allpass3, allpass3_io);
        {
            allpass4_io.in = (allpass4_io.in + allpass3_io.out);
        }
        AllpassFilter__main_2 (_state.allpass4, allpass4_io);
        {
            _io.audioOut = (_io.audioOut + allpass4_io.out);
        }
    }

    void CombFilter__main (_CombFilter_specialised_f32_1116_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain * _io.in));
            std__intrinsics___wrapped_pre_inc_1116 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1116 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1116 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1116 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1116});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1116}) : x;
    }

    void CombFilter__main_0 (_CombFilter_specialised_f32_1188_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_0 * _io.in));
            std__intrinsics___wrapped_pre_inc_1188 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1188 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1188 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1188 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1188});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1188}) : x;
    }

    void CombFilter__main_1 (_CombFilter_specialised_f32_1277_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_1 * _io.in));
            std__intrinsics___wrapped_pre_inc_1277 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1277 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1277 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1277 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1277});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1277}) : x;
    }

    void CombFilter__main_2 (_CombFilter_specialised_f32_1356_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_2 * _io.in));
            std__intrinsics___wrapped_pre_inc_1356 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1356 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1356 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1356 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1356});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1356}) : x;
    }

    void CombFilter__main_3 (_CombFilter_specialised_f32_1422_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_3 * _io.in));
            std__intrinsics___wrapped_pre_inc_1422 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1422 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1422 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1422 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1422});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1422}) : x;
    }

    void CombFilter__main_4 (_CombFilter_specialised_f32_1491_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_4 * _io.in));
            std__intrinsics___wrapped_pre_inc_1491 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1491 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1491 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1491 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1491});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1491}) : x;
    }

    void CombFilter__main_5 (_CombFilter_specialised_f32_1557_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_5 * _io.in));
            std__intrinsics___wrapped_pre_inc_1557 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1557 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1557 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1557 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1557});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1557}) : x;
    }

    void CombFilter__main_6 (_CombFilter_specialised_f32_1617_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_6 * _io.in));
            std__intrinsics___wrapped_pre_inc_1617 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1617 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1617 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1617 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1617});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1617}) : x;
    }

    void AllpassFilter__main (_AllpassFilter_specialised_f32_225_1_State& _state, _AllpassFilter_specialised_f32_225_1_IO& _io) noexcept
    {
        float  newValue;
        float  delayedValue;

        for (;;)
        {
            newValue = _io.in;
            delayedValue = _state.buffer[_state.index];
            _state.buffer[_state.index] = (newValue + (delayedValue * 0.5f));
            _io.out = (_io.out + (delayedValue - newValue));
            std__intrinsics___wrapped_pre_inc_225 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_225 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_225 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_225 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {225});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {225}) : x;
    }

    void AllpassFilter__main_0 (_AllpassFilter_specialised_f32_341_1_State& _state, _AllpassFilter_specialised_f32_225_1_IO& _io) noexcept
    {
        float  newValue;
        float  delayedValue;

        for (;;)
        {
            newValue = _io.in;
            delayedValue = _state.buffer[_state.index];
            _state.buffer[_state.index] = (newValue + (delayedValue * 0.5f));
            _io.out = (_io.out + (delayedValue - newValue));
            std__intrinsics___wrapped_pre_inc_341 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_341 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_341 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_341 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {341});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {341}) : x;
    }

    void AllpassFilter__main_1 (_AllpassFilter_specialised_f32_441_1_State& _state, _AllpassFilter_specialised_f32_225_1_IO& _io) noexcept
    {
        float  newValue;
        float  delayedValue;

        for (;;)
        {
            newValue = _io.in;
            delayedValue = _state.buffer[_state.index];
            _state.buffer[_state.index] = (newValue + (delayedValue * 0.5f));
            _io.out = (_io.out + (delayedValue - newValue));
            std__intrinsics___wrapped_pre_inc_441 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_441 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_441 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_441 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {441});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {441}) : x;
    }

    void AllpassFilter__main_2 (_AllpassFilter_specialised_f32_556_1_State& _state, _AllpassFilter_specialised_f32_225_1_IO& _io) noexcept
    {
        float  newValue;
        float  delayedValue;

        for (;;)
        {
            newValue = _io.in;
            delayedValue = _state.buffer[_state.index];
            _state.buffer[_state.index] = (newValue + (delayedValue * 0.5f));
            _io.out = (_io.out + (delayedValue - newValue));
            std__intrinsics___wrapped_pre_inc_556 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_556 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_556 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_556 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {556});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {556}) : x;
    }

    void main_2 (_MonoReverb_specialised_23_1_State& _state, _MonoReverb_specialised_0_1_IO& _io) noexcept
    {
        _CombFilter_specialised_f32_1116_1_IO  comb1_io;
        _CombFilter_specialised_f32_1116_1_IO  comb2_io;
        _CombFilter_specialised_f32_1116_1_IO  comb3_io;
        _CombFilter_specialised_f32_1116_1_IO  comb4_io;
        _CombFilter_specialised_f32_1116_1_IO  comb5_io;
        _CombFilter_specialised_f32_1116_1_IO  comb6_io;
        _CombFilter_specialised_f32_1116_1_IO  comb7_io;
        _CombFilter_specialised_f32_1116_1_IO  comb8_io;
        _AllpassFilter_specialised_f32_225_1_IO  allpass1_io;
        _AllpassFilter_specialised_f32_225_1_IO  allpass2_io;
        _AllpassFilter_specialised_f32_225_1_IO  allpass3_io;
        _AllpassFilter_specialised_f32_225_1_IO  allpass4_io;

        comb1_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb2_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb3_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb4_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb5_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb6_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb7_io = _CombFilter_specialised_f32_1116_1_IO {};
        comb8_io = _CombFilter_specialised_f32_1116_1_IO {};
        allpass1_io = _AllpassFilter_specialised_f32_225_1_IO {};
        allpass2_io = _AllpassFilter_specialised_f32_225_1_IO {};
        allpass3_io = _AllpassFilter_specialised_f32_225_1_IO {};
        allpass4_io = _AllpassFilter_specialised_f32_225_1_IO {};
        {
            comb1_io.in = (comb1_io.in + _io.audioIn);
            comb1_io.damping = (comb1_io.damping + _io.damping);
            comb1_io.feedback = (comb1_io.feedback + _io.feedback);
        }
        CombFilter__main_7 (_state.comb1, comb1_io);
        {
            comb2_io.in = (comb2_io.in + _io.audioIn);
            comb2_io.damping = (comb2_io.damping + _io.damping);
            comb2_io.feedback = (comb2_io.feedback + _io.feedback);
        }
        CombFilter__main_8 (_state.comb2, comb2_io);
        {
            comb3_io.in = (comb3_io.in + _io.audioIn);
            comb3_io.damping = (comb3_io.damping + _io.damping);
            comb3_io.feedback = (comb3_io.feedback + _io.feedback);
        }
        CombFilter__main_9 (_state.comb3, comb3_io);
        {
            comb4_io.in = (comb4_io.in + _io.audioIn);
            comb4_io.damping = (comb4_io.damping + _io.damping);
            comb4_io.feedback = (comb4_io.feedback + _io.feedback);
        }
        CombFilter__main_10 (_state.comb4, comb4_io);
        {
            comb5_io.in = (comb5_io.in + _io.audioIn);
            comb5_io.damping = (comb5_io.damping + _io.damping);
            comb5_io.feedback = (comb5_io.feedback + _io.feedback);
        }
        CombFilter__main_11 (_state.comb5, comb5_io);
        {
            comb6_io.in = (comb6_io.in + _io.audioIn);
            comb6_io.damping = (comb6_io.damping + _io.damping);
            comb6_io.feedback = (comb6_io.feedback + _io.feedback);
        }
        CombFilter__main_12 (_state.comb6, comb6_io);
        {
            comb7_io.in = (comb7_io.in + _io.audioIn);
            comb7_io.damping = (comb7_io.damping + _io.damping);
            comb7_io.feedback = (comb7_io.feedback + _io.feedback);
        }
        CombFilter__main_13 (_state.comb7, comb7_io);
        {
            comb8_io.in = (comb8_io.in + _io.audioIn);
            comb8_io.damping = (comb8_io.damping + _io.damping);
            comb8_io.feedback = (comb8_io.feedback + _io.feedback);
        }
        CombFilter__main_14 (_state.comb8, comb8_io);
        {
            allpass1_io.in = (allpass1_io.in + comb1_io.out);
            allpass1_io.in = (allpass1_io.in + comb2_io.out);
            allpass1_io.in = (allpass1_io.in + comb3_io.out);
            allpass1_io.in = (allpass1_io.in + comb4_io.out);
            allpass1_io.in = (allpass1_io.in + comb5_io.out);
            allpass1_io.in = (allpass1_io.in + comb6_io.out);
            allpass1_io.in = (allpass1_io.in + comb7_io.out);
            allpass1_io.in = (allpass1_io.in + comb8_io.out);
        }
        AllpassFilter__main_3 (_state.allpass1, allpass1_io);
        {
            allpass2_io.in = (allpass2_io.in + allpass1_io.out);
        }
        AllpassFilter__main_4 (_state.allpass2, allpass2_io);
        {
            allpass3_io.in = (allpass3_io.in + allpass2_io.out);
        }
        AllpassFilter__main_5 (_state.allpass3, allpass3_io);
        {
            allpass4_io.in = (allpass4_io.in + allpass3_io.out);
        }
        AllpassFilter__main_6 (_state.allpass4, allpass4_io);
        {
            _io.audioOut = (_io.audioOut + allpass4_io.out);
        }
    }

    void CombFilter__main_7 (_CombFilter_specialised_f32_1139_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_7 * _io.in));
            std__intrinsics___wrapped_pre_inc_1139 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1139 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1139 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1139 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1139});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1139}) : x;
    }

    void CombFilter__main_8 (_CombFilter_specialised_f32_1211_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_8 * _io.in));
            std__intrinsics___wrapped_pre_inc_1211 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1211 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1211 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1211 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1211});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1211}) : x;
    }

    void CombFilter__main_9 (_CombFilter_specialised_f32_1300_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_9 * _io.in));
            std__intrinsics___wrapped_pre_inc_1300 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1300 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1300 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1300 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1300});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1300}) : x;
    }

    void CombFilter__main_10 (_CombFilter_specialised_f32_1379_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_10 * _io.in));
            std__intrinsics___wrapped_pre_inc_1379 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1379 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1379 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1379 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1379});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1379}) : x;
    }

    void CombFilter__main_11 (_CombFilter_specialised_f32_1445_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_11 * _io.in));
            std__intrinsics___wrapped_pre_inc_1445 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1445 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1445 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1445 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1445});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1445}) : x;
    }

    void CombFilter__main_12 (_CombFilter_specialised_f32_1514_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_12 * _io.in));
            std__intrinsics___wrapped_pre_inc_1514 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1514 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1514 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1514 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1514});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1514}) : x;
    }

    void CombFilter__main_13 (_CombFilter_specialised_f32_1580_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_13 * _io.in));
            std__intrinsics___wrapped_pre_inc_1580 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1580 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1580 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1580 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1580});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1580}) : x;
    }

    void CombFilter__main_14 (_CombFilter_specialised_f32_1640_1_State& _state, _CombFilter_specialised_f32_1116_1_IO& _io) noexcept
    {
        float  delayedValue;

        for (;;)
        {
            delayedValue = _state.buffer[_state.index];
            _io.out = (_io.out + delayedValue);
            _state.last = ((_state.last * _io.damping) + (delayedValue * (1.0f - _io.damping)));
            _state.buffer[_state.index] = ((_state.last * _io.feedback) + (g_gain_14 * _io.in));
            std__intrinsics___wrapped_pre_inc_1640 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_1640 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_1640 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_1640 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {1640});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {1640}) : x;
    }

    void AllpassFilter__main_3 (_AllpassFilter_specialised_f32_248_1_State& _state, _AllpassFilter_specialised_f32_225_1_IO& _io) noexcept
    {
        float  newValue;
        float  delayedValue;

        for (;;)
        {
            newValue = _io.in;
            delayedValue = _state.buffer[_state.index];
            _state.buffer[_state.index] = (newValue + (delayedValue * 0.5f));
            _io.out = (_io.out + (delayedValue - newValue));
            std__intrinsics___wrapped_pre_inc_248 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_248 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_248 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_248 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {248});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {248}) : x;
    }

    void AllpassFilter__main_4 (_AllpassFilter_specialised_f32_364_1_State& _state, _AllpassFilter_specialised_f32_225_1_IO& _io) noexcept
    {
        float  newValue;
        float  delayedValue;

        for (;;)
        {
            newValue = _io.in;
            delayedValue = _state.buffer[_state.index];
            _state.buffer[_state.index] = (newValue + (delayedValue * 0.5f));
            _io.out = (_io.out + (delayedValue - newValue));
            std__intrinsics___wrapped_pre_inc_364 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_364 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_364 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_364 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {364});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {364}) : x;
    }

    void AllpassFilter__main_5 (_AllpassFilter_specialised_f32_464_1_State& _state, _AllpassFilter_specialised_f32_225_1_IO& _io) noexcept
    {
        float  newValue;
        float  delayedValue;

        for (;;)
        {
            newValue = _io.in;
            delayedValue = _state.buffer[_state.index];
            _state.buffer[_state.index] = (newValue + (delayedValue * 0.5f));
            _io.out = (_io.out + (delayedValue - newValue));
            std__intrinsics___wrapped_pre_inc_464 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_464 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_464 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_464 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {464});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {464}) : x;
    }

    void AllpassFilter__main_6 (_AllpassFilter_specialised_f32_579_1_State& _state, _AllpassFilter_specialised_f32_225_1_IO& _io) noexcept
    {
        float  newValue;
        float  delayedValue;

        for (;;)
        {
            newValue = _io.in;
            delayedValue = _state.buffer[_state.index];
            _state.buffer[_state.index] = (newValue + (delayedValue * 0.5f));
            _io.out = (_io.out + (delayedValue - newValue));
            std__intrinsics___wrapped_pre_inc_579 (_state.index);
            return;
        }
    }

    int32_t std__intrinsics___wrapped_pre_inc_579 (int32_t& n) noexcept
    {
        int32_t  result;

        result = std__intrinsics___wrap_579 (intrinsics::add (n, int32_t {1}));
        n = result;
        return result;
    }

    int32_t std__intrinsics___wrap_579 (int32_t n) noexcept
    {
        int32_t  x;

        x = intrinsics::modulo (n, int32_t {579});
        return (x < int32_t {0}) ? intrinsics::add (x, int32_t {579}) : x;
    }

    void WetDryMixer__main (_WetDryMixer_1_State& _state, _WetDryMixer_1_IO& _io) noexcept
    {
        float  wetGain1;
        float  wetGain2;
        Vector<float, 2>  _temp;
        Vector<float, 2>  wet;
        Vector<float, 2>  _temp_0;
        float  _temp_1;

        for (;;)
        {
            wetGain1 = _io.wetGain * (1.0f + _io.width);
            wetGain2 = _io.wetGain * (1.0f - _io.width);
            _temp[int32_t {0}] = ((_io.audioInWetL * wetGain1) + (_io.audioInWetR * wetGain2));
            _temp[int32_t {1}] = ((_io.audioInWetR * wetGain1) + (_io.audioInWetL * wetGain2));
            wet = _temp;
            _temp_1 = _io.dryGain * _io.audioInDry;
            _temp_0[int32_t {0}] = _temp_1;
            _temp_0[int32_t {1}] = _temp_1;
            _io.out = (_io.out + (wet + _temp_0));
            return;
        }
    }

    //==============================================================================
    const char* getStringForHandle (uint32_t handle, size_t& stringLength)
    {
        (void) handle; (void) stringLength;
        return "";
    }

    //==============================================================================
    #define OFFSETOF(type, member)   ((size_t)((char *)&(*(type *)nullptr).member))
    static _Freeverb_State& state_upcast_struct_Freeverb_State_st_YC9hVb (_ParameterScaler_1_State& child) { return *reinterpret_cast<_Freeverb_State*> (reinterpret_cast<char*> (std::addressof (child)) - (OFFSETOF (_Freeverb_State, parameterScaler))); }

    //==============================================================================
    static constexpr float g_smoothingTimeSeconds { 0.02f };
    double g__frequency {};
    static constexpr float g_roomScaleFactor { 0.28f };
    static constexpr float g_roomOffset { 0.7f };
    static constexpr float g_smoothingTimeSeconds_0 { 0.02f };
    static constexpr float g_dampScaleFactor { 0.4f };
    static constexpr float g_smoothingTimeSeconds_1 { 0.02f };
    static constexpr float g_smoothingTimeSeconds_2 { 0.02f };
    static constexpr float g_wetScaleFactor { 1.5f };
    static constexpr float g_smoothingTimeSeconds_3 { 0.02f };
    static constexpr float g_dryScaleFactor { 2.0f };
    int32_t g__sessionID {};
    static constexpr float g_initialValue { 0.0f };
    static constexpr float g_initialValue_0 { 0.0f };
    static constexpr float g_initialValue_1 { 0.0f };
    static constexpr float g_initialValue_2 { 0.0f };
    static constexpr float g_initialValue_3 { 0.0f };
    static constexpr float g_gain { 0.015f };
    static constexpr float g_gain_0 { 0.015f };
    static constexpr float g_gain_1 { 0.015f };
    static constexpr float g_gain_2 { 0.015f };
    static constexpr float g_gain_3 { 0.015f };
    static constexpr float g_gain_4 { 0.015f };
    static constexpr float g_gain_5 { 0.015f };
    static constexpr float g_gain_6 { 0.015f };
    static constexpr float g_gain_7 { 0.015f };
    static constexpr float g_gain_8 { 0.015f };
    static constexpr float g_gain_9 { 0.015f };
    static constexpr float g_gain_10 { 0.015f };
    static constexpr float g_gain_11 { 0.015f };
    static constexpr float g_gain_12 { 0.015f };
    static constexpr float g_gain_13 { 0.015f };
    static constexpr float g_gain_14 { 0.015f };

    //==============================================================================
    #if __clang__
     #pragma clang diagnostic pop
    #elif __GNUC__
     #pragma GCC diagnostic pop
    #else
     #pragma warning (pop)
    #endif
};

