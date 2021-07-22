/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/file.h>
#include <android/hardware/automotive/vehicle/2.0/types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <vhal_v2_0/ConcurrentQueue.h>
#include <vhal_v2_0/DefaultConfig.h>
#include <vhal_v2_0/DefaultVehicleConnector.h>
#include <vhal_v2_0/DefaultVehicleHal.h>
#include <vhal_v2_0/PropertyUtils.h>
#include <vhal_v2_0/VehicleObjectPool.h>
#include <vhal_v2_0/VehiclePropertyStore.h>

namespace {

using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::automotive::vehicle::V2_0::FuelType;
using ::android::hardware::automotive::vehicle::V2_0::recyclable_ptr;
using ::android::hardware::automotive::vehicle::V2_0::StatusCode;
using ::android::hardware::automotive::vehicle::V2_0::VehicleHwKeyInputAction;
using ::android::hardware::automotive::vehicle::V2_0::VehiclePropConfig;
using ::android::hardware::automotive::vehicle::V2_0::VehicleProperty;
using ::android::hardware::automotive::vehicle::V2_0::VehiclePropertyStatus;
using ::android::hardware::automotive::vehicle::V2_0::VehiclePropertyStore;
using ::android::hardware::automotive::vehicle::V2_0::VehiclePropValue;
using ::android::hardware::automotive::vehicle::V2_0::VehiclePropValuePool;
using ::android::hardware::automotive::vehicle::V2_0::impl::DefaultVehicleConnector;
using ::android::hardware::automotive::vehicle::V2_0::impl::DefaultVehicleHal;
using ::android::hardware::automotive::vehicle::V2_0::impl::HVAC_LEFT;
using ::android::hardware::automotive::vehicle::V2_0::impl::kMixedTypePropertyForTest;

using ::testing::HasSubstr;

using VehiclePropValuePtr = recyclable_ptr<VehiclePropValue>;

// The maximum length of property ID in string.
const size_t MAX_PROP_ID_LENGTH = 100;

class DefaultVhalImplTest : public ::testing::Test {
  public:
    ~DefaultVhalImplTest() {
        mEventQueue.deactivate();
        mHeartBeatQueue.deactivate();
        // Destroy mHal before destroying its dependencies.
        mHal.reset();
        mConnector.reset();
        mPropStore.reset();
    }

  protected:
    void SetUp() override {
        mPropStore.reset(new VehiclePropertyStore);
        mConnector.reset(new DefaultVehicleConnector);
        mConnector->setValuePool(&mValueObjectPool);
        mHal.reset(new DefaultVehicleHal(mPropStore.get(), mConnector.get()));
        mHal->init(&mValueObjectPool,
                   std::bind(&DefaultVhalImplTest::onHalEvent, this, std::placeholders::_1),
                   std::bind(&DefaultVhalImplTest::onHalPropertySetError, this,
                             std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

  private:
    void onHalEvent(VehiclePropValuePtr v) {
        if (v->prop != toInt(VehicleProperty::VHAL_HEARTBEAT)) {
            // Ignore heartbeat properties.
            mEventQueue.push(std::move(v));
        } else {
            mHeartBeatQueue.push(std::move(v));
        }
    }

    void onHalPropertySetError(StatusCode /*errorCode*/, int32_t /*property*/, int32_t /*areaId*/) {
    }

  protected:
    std::unique_ptr<DefaultVehicleHal> mHal;
    std::unique_ptr<DefaultVehicleConnector> mConnector;
    std::unique_ptr<VehiclePropertyStore> mPropStore;
    VehiclePropValuePool mValueObjectPool;
    android::ConcurrentQueue<VehiclePropValuePtr> mEventQueue;
    android::ConcurrentQueue<VehiclePropValuePtr> mHeartBeatQueue;
};

TEST_F(DefaultVhalImplTest, testListProperties) {
    std::vector<VehiclePropConfig> configs = mHal->listProperties();

    EXPECT_EQ((size_t)121, configs.size());
}

TEST_F(DefaultVhalImplTest, testGetDefaultPropertyFloat) {
    VehiclePropValue value;
    StatusCode status;
    value.prop = toInt(VehicleProperty::INFO_FUEL_CAPACITY);

    auto gotValue = mHal->get(value, &status);

    EXPECT_EQ(StatusCode::OK, status);
    ASSERT_EQ((unsigned int)1, gotValue->value.floatValues.size());
    EXPECT_EQ(15000.0f, gotValue->value.floatValues[0]);
}

TEST_F(DefaultVhalImplTest, testGetDefaultPropertyEnum) {
    VehiclePropValue value;
    StatusCode status;
    value.prop = toInt(VehicleProperty::INFO_FUEL_TYPE);

    auto gotValue = mHal->get(value, &status);

    EXPECT_EQ(StatusCode::OK, status);
    ASSERT_EQ((unsigned int)1, gotValue->value.int32Values.size());
    EXPECT_EQ((int)FuelType::FUEL_TYPE_UNLEADED, gotValue->value.int32Values[0]);
}

TEST_F(DefaultVhalImplTest, testGetDefaultPropertyInt) {
    VehiclePropValue value;
    StatusCode status;
    value.prop = toInt(VehicleProperty::INFO_MODEL_YEAR);

    auto gotValue = mHal->get(value, &status);

    EXPECT_EQ(StatusCode::OK, status);
    ASSERT_EQ((unsigned int)1, gotValue->value.int32Values.size());
    EXPECT_EQ(2020, gotValue->value.int32Values[0]);
}

TEST_F(DefaultVhalImplTest, testGetDefaultPropertyString) {
    VehiclePropValue value;
    StatusCode status;
    value.prop = toInt(VehicleProperty::INFO_MAKE);

    auto gotValue = mHal->get(value, &status);

    EXPECT_EQ(StatusCode::OK, status);
    EXPECT_EQ("Toy Vehicle", gotValue->value.stringValue);
}

TEST_F(DefaultVhalImplTest, testGetUnknownProperty) {
    VehiclePropValue value;
    StatusCode status;
    value.prop = 0;

    auto gotValue = mHal->get(value, &status);

    EXPECT_EQ(StatusCode::INVALID_ARG, status);
}

TEST_F(DefaultVhalImplTest, testSetFloat) {
    VehiclePropValue value;
    value.prop = toInt(VehicleProperty::INFO_FUEL_CAPACITY);
    value.value.floatValues.resize(1);
    value.value.floatValues[0] = 1.0f;

    StatusCode status = mHal->set(value);
    ASSERT_EQ(StatusCode::OK, status);

    auto gotValue = mHal->get(value, &status);
    EXPECT_EQ(StatusCode::OK, status);
    ASSERT_EQ((unsigned int)1, gotValue->value.floatValues.size());
    EXPECT_EQ(1.0f, gotValue->value.floatValues[0]);
}

TEST_F(DefaultVhalImplTest, testSetEnum) {
    VehiclePropValue value;
    value.prop = toInt(VehicleProperty::INFO_FUEL_TYPE);
    value.value.int32Values.resize(1);
    value.value.int32Values[0] = (int)FuelType::FUEL_TYPE_LEADED;

    StatusCode status = mHal->set(value);
    ASSERT_EQ(StatusCode::OK, status);

    auto gotValue = mHal->get(value, &status);
    EXPECT_EQ(StatusCode::OK, status);
    ASSERT_EQ((unsigned int)1, gotValue->value.int32Values.size());
    EXPECT_EQ((int)FuelType::FUEL_TYPE_LEADED, gotValue->value.int32Values[0]);
}

TEST_F(DefaultVhalImplTest, testSetInt) {
    VehiclePropValue value;
    value.prop = toInt(VehicleProperty::INFO_MODEL_YEAR);
    value.value.int32Values.resize(1);
    value.value.int32Values[0] = 2021;

    StatusCode status = mHal->set(value);
    EXPECT_EQ(StatusCode::OK, status);

    auto gotValue = mHal->get(value, &status);
    EXPECT_EQ(StatusCode::OK, status);
    EXPECT_EQ((unsigned int)1, gotValue->value.int32Values.size());
    EXPECT_EQ(2021, gotValue->value.int32Values[0]);
}

TEST_F(DefaultVhalImplTest, testSetString) {
    VehiclePropValue value;
    value.prop = toInt(VehicleProperty::INFO_MAKE);
    value.value.stringValue = "My Vehicle";

    StatusCode status = mHal->set(value);
    ASSERT_EQ(StatusCode::OK, status);

    auto gotValue = mHal->get(value, &status);
    EXPECT_EQ(StatusCode::OK, status);
    EXPECT_EQ("My Vehicle", gotValue->value.stringValue);
}

TEST_F(DefaultVhalImplTest, testSetMixed) {
    VehiclePropValue value;
    value.prop = kMixedTypePropertyForTest;
    // mixed prop.
    // .configArray = {1, 1, 0, 2, 0, 0, 1, 0, 0}
    // 1 string, 1 int, 0 bool, 2 ints, 0 int64, 0 int64s, 1 float, 0 floats, 0 bytes
    value.value.stringValue = "test";
    value.value.int32Values.resize(3);
    value.value.int32Values[0] = 1;
    value.value.int32Values[1] = 2;
    value.value.int32Values[2] = 3;
    value.value.floatValues.resize(1);
    value.value.floatValues[0] = 1.0f;

    StatusCode status = mHal->set(value);
    ASSERT_EQ(StatusCode::OK, status);

    auto gotValue = mHal->get(value, &status);
    EXPECT_EQ(StatusCode::OK, status);
    EXPECT_EQ("test", gotValue->value.stringValue);
    ASSERT_EQ((size_t)3, gotValue->value.int32Values.size());
    EXPECT_EQ(1, gotValue->value.int32Values[0]);
    EXPECT_EQ(2, gotValue->value.int32Values[1]);
    EXPECT_EQ(3, gotValue->value.int32Values[2]);
    ASSERT_EQ((size_t)1, gotValue->value.floatValues.size());
    EXPECT_EQ(1.0f, gotValue->value.floatValues[0]);
}

TEST_F(DefaultVhalImplTest, testSetUnknownProperty) {
    VehiclePropValue value;
    value.prop = 0;

    EXPECT_EQ(StatusCode::INVALID_ARG, mHal->set(value));
}

TEST_F(DefaultVhalImplTest, testSetStatusNotAllowed) {
    VehiclePropValue value;
    value.prop = toInt(VehicleProperty::INFO_FUEL_CAPACITY);
    value.status = VehiclePropertyStatus::UNAVAILABLE;
    value.value.floatValues.resize(1);
    value.value.floatValues[0] = 1.0f;

    StatusCode status = mHal->set(value);

    EXPECT_EQ(StatusCode::INVALID_ARG, status);
}

TEST_F(DefaultVhalImplTest, testSubscribe) {
    // Clear existing events.
    mEventQueue.flush();

    auto status = mHal->subscribe(toInt(VehicleProperty::PERF_VEHICLE_SPEED), 10);

    ASSERT_EQ(StatusCode::OK, status);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Modify the speed after 0.5 seconds.
    VehiclePropValue value;
    value.prop = toInt(VehicleProperty::PERF_VEHICLE_SPEED);
    value.value.floatValues.resize(1);
    value.value.floatValues[0] = 1.0f;
    ASSERT_EQ(StatusCode::OK, mHal->set(value));

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto events = mEventQueue.flush();
    ASSERT_LE((size_t)10, events.size());

    // The first event should be the default value.
    ASSERT_EQ((size_t)1, events[0]->value.floatValues.size());
    EXPECT_EQ(0.0f, events[0]->value.floatValues[0]);
    // The last event should be the value after update.
    ASSERT_EQ((size_t)1, events[events.size() - 1]->value.floatValues.size());
    EXPECT_EQ(1.0f, events[events.size() - 1]->value.floatValues[0]);
}

TEST_F(DefaultVhalImplTest, testSubscribeInvalidProp) {
    EXPECT_EQ(StatusCode::INVALID_ARG, mHal->subscribe(toInt(VehicleProperty::INFO_MAKE), 10));
}

TEST_F(DefaultVhalImplTest, testSubscribeSampleRateOutOfRange) {
    EXPECT_EQ(StatusCode::INVALID_ARG,
              mHal->subscribe(toInt(VehicleProperty::PERF_VEHICLE_SPEED), 10.1));
    EXPECT_EQ(StatusCode::INVALID_ARG,
              mHal->subscribe(toInt(VehicleProperty::PERF_VEHICLE_SPEED), 0.5));
}

TEST_F(DefaultVhalImplTest, testUnsubscribe) {
    auto status = mHal->subscribe(toInt(VehicleProperty::PERF_VEHICLE_SPEED), 10);
    ASSERT_EQ(StatusCode::OK, status);

    // Wait for 0.5 seconds to generate some events.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    status = mHal->unsubscribe(toInt(VehicleProperty::PERF_VEHICLE_SPEED));
    ASSERT_EQ(StatusCode::OK, status);

    // Clear all the events.
    mEventQueue.flush();

    // Wait for 0.5 seconds.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // There should be no new events generated.
    auto events = mEventQueue.flush();
    EXPECT_EQ((size_t)0, events.size());
}

TEST_F(DefaultVhalImplTest, testUnsubscribeInvalidProp) {
    EXPECT_EQ(StatusCode::INVALID_ARG, mHal->unsubscribe(toInt(VehicleProperty::INFO_MAKE)));
}

int createMemfd(hidl_handle* fd) {
    native_handle_t* handle = native_handle_create(/*numFds=*/1, /*numInts=*/0);
    int memfd = memfd_create("memfile", 0);
    handle->data[0] = dup(memfd);
    fd->setTo(handle, /*shouldOwn=*/true);
    return memfd;
}

TEST_F(DefaultVhalImplTest, testDump) {
    hidl_vec<hidl_string> options;
    hidl_handle fd = {};
    int memfd = createMemfd(&fd);

    ASSERT_TRUE(mHal->dump(fd, options));

    lseek(memfd, 0, SEEK_SET);
    char buf[10240] = {};
    read(memfd, buf, sizeof(buf));
    close(memfd);

    // Read one property and check that it is in the dumped info.
    VehiclePropValue value;
    StatusCode status;
    value.prop = toInt(VehicleProperty::INFO_MAKE);
    auto gotValue = mHal->get(value, &status);
    ASSERT_EQ(StatusCode::OK, status);
    // Server side prop store does not have timestamp.
    gotValue->timestamp = 0;

    std::string infoMake = toString(*gotValue);
    EXPECT_THAT(std::string(buf), HasSubstr(infoMake));
}

class DefaultVhalImplSetInvalidPropTest : public DefaultVhalImplTest,
                                          public testing::WithParamInterface<VehiclePropValue> {};

std::vector<VehiclePropValue> GenSetInvalidPropParams() {
    std::vector<VehiclePropValue> props;
    // int prop with no value.
    VehiclePropValue intProp = {.prop = toInt(VehicleProperty::INFO_MODEL_YEAR)};
    props.push_back(intProp);

    // int prop with more than one value.
    VehiclePropValue intPropWithValues = {.prop = toInt(VehicleProperty::INFO_MODEL_YEAR)};
    intPropWithValues.value.int32Values.resize(2);
    props.push_back(intPropWithValues);

    // int vec prop with no value.
    VehiclePropValue intVecProp = {.prop = toInt(VehicleProperty::INFO_FUEL_TYPE)};
    props.push_back(intVecProp);

    // int64 prop with no value.
    VehiclePropValue int64Prop = {.prop = toInt(VehicleProperty::EPOCH_TIME)};
    props.push_back(int64Prop);

    // int64 prop with more than one value.
    VehiclePropValue int64PropWithValues = {.prop = toInt(VehicleProperty::EPOCH_TIME)};
    int64PropWithValues.value.int64Values.resize(2);
    props.push_back(int64PropWithValues);

    // int64 vec prop with no value.
    VehiclePropValue int64VecProp = {.prop = toInt(VehicleProperty::WHEEL_TICK)};
    props.push_back(int64VecProp);

    // float prop with no value.
    VehiclePropValue floatProp = {.prop = toInt(VehicleProperty::INFO_FUEL_CAPACITY)};
    props.push_back(floatProp);

    // float prop with more than one value.
    VehiclePropValue floatPropWithValues = {.prop = toInt(VehicleProperty::INFO_FUEL_CAPACITY)};
    floatPropWithValues.value.floatValues.resize(2);
    props.push_back(floatPropWithValues);

    // float vec prop with no value.
    VehiclePropValue floatVecProp = {
            .prop = toInt(VehicleProperty::HVAC_TEMPERATURE_VALUE_SUGGESTION)};
    props.push_back(floatVecProp);

    // bool prop with no value.
    VehiclePropValue boolProp = {
            .prop = toInt(VehicleProperty::FUEL_CONSUMPTION_UNITS_DISTANCE_OVER_VOLUME)};
    props.push_back(boolProp);

    // bool prop with more than one value.
    VehiclePropValue boolPropWithValues = {
            .prop = toInt(VehicleProperty::FUEL_CONSUMPTION_UNITS_DISTANCE_OVER_VOLUME)};
    boolPropWithValues.value.int32Values.resize(2);
    props.push_back(boolPropWithValues);

    // mixed prop.
    // .configArray = {1, 1, 0, 2, 0, 0, 1, 0, 0}
    // 1 string, 1 int, 0 bool, 2 ints, 0 int64, 0 int64s, 1 float, 0 floats, 0 bytes
    VehiclePropValue mixedProp1 = {.prop = kMixedTypePropertyForTest};
    // Expect 1 bool, and 2 ints, we only have 1 value.
    mixedProp1.value.int32Values.resize(1);
    mixedProp1.value.floatValues.resize(1);
    props.push_back(mixedProp1);

    VehiclePropValue mixedProp2 = {.prop = kMixedTypePropertyForTest};
    mixedProp2.value.int32Values.resize(3);
    // Missing float value.
    mixedProp2.value.floatValues.resize(0);
    props.push_back(mixedProp2);

    return props;
}

TEST_P(DefaultVhalImplSetInvalidPropTest, testSetInvalidPropValue) {
    VehiclePropValue value = GetParam();

    StatusCode status = mHal->set(value);

    EXPECT_EQ(StatusCode::INVALID_ARG, status);
}

INSTANTIATE_TEST_SUITE_P(DefaultVhalImplSetInvalidPropTests, DefaultVhalImplSetInvalidPropTest,
                         testing::ValuesIn(GenSetInvalidPropParams()));

struct SetPropRangeTestCase {
    std::string name;
    VehiclePropValue prop;
    StatusCode code;
};

class DefaultVhalImplSetPropRangeTest : public DefaultVhalImplTest,
                                        public testing::WithParamInterface<SetPropRangeTestCase> {};

std::vector<SetPropRangeTestCase> GenSetPropRangeParams() {
    std::vector<SetPropRangeTestCase> tc;
    VehiclePropValue intPropNormal = {.prop = toInt(VehicleProperty::HVAC_FAN_SPEED),
                                      .areaId = HVAC_LEFT,
                                      // min: 1, max: 7
                                      .value.int32Values = {3}};
    tc.push_back({"normal_case_int", intPropNormal, StatusCode::OK});

    VehiclePropValue intPropSmall = {.prop = toInt(VehicleProperty::HVAC_FAN_SPEED),
                                     .areaId = HVAC_LEFT,
                                     // min: 1, max: 7
                                     .value.int32Values = {0}};
    tc.push_back({"normal_case_int_too_small", intPropSmall, StatusCode::INVALID_ARG});

    VehiclePropValue intPropLarge = {.prop = toInt(VehicleProperty::HVAC_FAN_SPEED),
                                     .areaId = HVAC_LEFT,
                                     // min: 1, max: 7
                                     .value.int32Values = {8}};
    tc.push_back({"normal_case_int_too_large", intPropLarge, StatusCode::INVALID_ARG});

    VehiclePropValue floatPropNormal = {.prop = toInt(VehicleProperty::HVAC_TEMPERATURE_SET),
                                        .areaId = HVAC_LEFT,
                                        // min: 16, max: 32
                                        .value.floatValues = {26}};
    tc.push_back({"normal_case_float", floatPropNormal, StatusCode::OK});
    VehiclePropValue floatPropSmall = {.prop = toInt(VehicleProperty::HVAC_TEMPERATURE_SET),
                                       .areaId = HVAC_LEFT,
                                       // min: 16, max: 32
                                       .value.floatValues = {15.5}};
    tc.push_back({"normal_case_float_too_small", floatPropSmall, StatusCode::INVALID_ARG});
    VehiclePropValue floatPropLarge = {.prop = toInt(VehicleProperty::HVAC_TEMPERATURE_SET),
                                       .areaId = HVAC_LEFT,
                                       // min: 16, max: 32
                                       .value.floatValues = {32.6}};
    tc.push_back({"normal_case_float_too_large", floatPropLarge, StatusCode::INVALID_ARG});

    return tc;
}

TEST_P(DefaultVhalImplSetPropRangeTest, testSetPropRange) {
    SetPropRangeTestCase tc = GetParam();

    StatusCode status = mHal->set(tc.prop);

    EXPECT_EQ(tc.code, status);
}

INSTANTIATE_TEST_SUITE_P(
        DefaultVhalImplSetPropRangeTests, DefaultVhalImplSetPropRangeTest,
        testing::ValuesIn(GenSetPropRangeParams()),
        [](const testing::TestParamInfo<DefaultVhalImplSetPropRangeTest::ParamType>& info) {
            return info.param.name;
        });

std::string getPropIdString(VehicleProperty prop) {
    char s[MAX_PROP_ID_LENGTH] = {};
    snprintf(s, sizeof(s), "%d", toInt(prop));
    return std::string(s);
}

struct OptionsTestCase {
    std::string name;
    hidl_vec<hidl_string> options;
    std::string expectMsg;
};

class DefaultVhalImplOptionsTest : public DefaultVhalImplTest,
                                   public testing::WithParamInterface<OptionsTestCase> {};

TEST_P(DefaultVhalImplOptionsTest, testInvalidOptions) {
    auto tc = GetParam();
    hidl_handle fd = {};
    int memfd = createMemfd(&fd);

    bool shouldDump = mHal->dump(fd, tc.options);

    EXPECT_FALSE(shouldDump);
    char buf[10240] = {};
    lseek(memfd, 0, SEEK_SET);
    read(memfd, buf, sizeof(buf));
    EXPECT_THAT(std::string(buf), HasSubstr(tc.expectMsg));
}

std::vector<OptionsTestCase> GenInvalidOptions() {
    return {{"no_command", {"--debughal"}, "No command specified"},
            {"unknown_command", {"--debughal", "--unknown"}, "Unknown command: \"--unknown\""},
            {"help", {"--debughal", "--help"}, "Help:"},
            {"genfakedata_no_subcommand",
             {"--debughal", "--genfakedata"},
             "No subcommand specified for genfakedata"},
            {"genfakedata_unknown_subcommand",
             {"--debughal", "--genfakedata", "--unknown"},
             "Unknown command: \"--unknown\""},
            {"genfakedata_start_linear_no_args",
             {"--debughal", "--genfakedata", "--startlinear"},
             "incorrect argument count"},
            {"genfakedata_start_linear_invalid_propId",
             {"--debughal", "--genfakedata", "--startlinear", "abcd", "0.1", "0.1", "0.1", "0.1",
              "100000000"},
             "failed to parse propdID as int: \"abcd\""},
            {"genfakedata_start_linear_invalid_middleValue",
             {"--debughal", "--genfakedata", "--startlinear", "1", "abcd", "0.1", "0.1", "0.1",
              "100000000"},
             "failed to parse middleValue as float: \"abcd\""},
            {"genfakedata_start_linear_invalid_currentValue",
             {"--debughal", "--genfakedata", "--startlinear", "1", "0.1", "abcd", "0.1", "0.1",
              "100000000"},
             "failed to parse currentValue as float: \"abcd\""},
            {"genfakedata_start_linear_invalid_dispersion",
             {"--debughal", "--genfakedata", "--startlinear", "1", "0.1", "0.1", "abcd", "0.1",
              "100000000"},
             "failed to parse dispersion as float: \"abcd\""},
            {"genfakedata_start_linear_invalid_increment",
             {"--debughal", "--genfakedata", "--startlinear", "1", "0.1", "0.1", "0.1", "abcd",
              "100000000"},
             "failed to parse increment as float: \"abcd\""},
            {"genfakedata_start_linear_invalid_interval",
             {"--debughal", "--genfakedata", "--startlinear", "1", "0.1", "0.1", "0.1", "0.1",
              "0.1"},
             "failed to parse interval as int: \"0.1\""},
            {"genfakedata_stop_linear_no_args",
             {"--debughal", "--genfakedata", "--stoplinear"},
             "incorrect argument count"},
            {"genfakedata_stop_linear_invalid_propId",
             {"--debughal", "--genfakedata", "--stoplinear", "abcd"},
             "failed to parse propdID as int: \"abcd\""},
            {"genfakedata_startjson_no_args",
             {"--debughal", "--genfakedata", "--startjson"},
             "incorrect argument count"},
            {"genfakedata_startjson_invalid_repetition",
             {"--debughal", "--genfakedata", "--startjson", "file", "0.1"},
             "failed to parse repetition as int: \"0.1\""},
            {"genfakedata_startjson_invalid_json_file",
             {"--debughal", "--genfakedata", "--startjson", "file", "1"},
             "invalid JSON file"},
            {"genfakedata_stopjson_no_args",
             {"--debughal", "--genfakedata", "--stopjson"},
             "incorrect argument count"},
            {"genfakedata_keypress_no_args",
             {"--debughal", "--genfakedata", "--keypress"},
             "incorrect argument count"},
            {"genfakedata_keypress_invalid_keyCode",
             {"--debughal", "--genfakedata", "--keypress", "0.1", "1"},
             "failed to parse keyCode as int: \"0.1\""},
            {"genfakedata_keypress_invalid_display",
             {"--debughal", "--genfakedata", "--keypress", "1", "0.1"},
             "failed to parse display as int: \"0.1\""}};
}

INSTANTIATE_TEST_SUITE_P(
        DefaultVhalImplOptionsTests, DefaultVhalImplOptionsTest,
        testing::ValuesIn(GenInvalidOptions()),
        [](const testing::TestParamInfo<DefaultVhalImplOptionsTest::ParamType>& info) {
            return info.param.name;
        });

TEST_F(DefaultVhalImplTest, testDebugGenFakeDataLinear) {
    // Start a fake linear data generator for vehicle speed at 0.1s interval.
    // range: 0 - 100, current value: 30, step: 20.
    hidl_vec<hidl_string> options = {"--debughal",
                                     "--genfakedata",
                                     "--startlinear",
                                     getPropIdString(VehicleProperty::PERF_VEHICLE_SPEED),
                                     /*middleValue=*/"50",
                                     /*currentValue=*/"30",
                                     /*dispersion=*/"50",
                                     /*increment=*/"20",
                                     /*interval=*/"100000000"};
    hidl_handle fd = {};
    int memfd = createMemfd(&fd);
    // Clear existing events.
    mEventQueue.flush();

    EXPECT_FALSE(mHal->dump(fd, options));

    lseek(memfd, 0, SEEK_SET);
    char buf[10240] = {};
    // The dumped info should be empty.
    read(memfd, buf, sizeof(buf));
    EXPECT_STREQ("", buf);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto events = mEventQueue.flush();
    // We should get 10 events ideally, but let's be safe here.
    ASSERT_LE((size_t)5, events.size());
    int32_t value = 30;
    for (size_t i = 0; i < 5; i++) {
        ASSERT_EQ((size_t)1, events[i]->value.floatValues.size());
        EXPECT_EQ((float)value, events[i]->value.floatValues[0]);
        value = (value + 20) % 100;
    }

    // Stop the linear generator.
    options = {"--debughal", "--genfakedata", "--stoplinear",
               getPropIdString(VehicleProperty::PERF_VEHICLE_SPEED)};
    EXPECT_FALSE(mHal->dump(fd, options));

    // The dumped info should be empty.
    lseek(memfd, 0, SEEK_SET);
    read(memfd, buf, sizeof(buf));
    EXPECT_STREQ("", buf);

    close(memfd);

    // Clear existing events.
    mEventQueue.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // There should be no new events generated.
    EXPECT_EQ((size_t)0, mEventQueue.flush().size());
}

std::string getTestFilePath(const char* filename) {
    static std::string baseDir = android::base::GetExecutableDirectory();
    return baseDir + "/" + filename;
}

TEST_F(DefaultVhalImplTest, testDebugGenFakeDataJson) {
    hidl_vec<hidl_string> options = {"--debughal", "--genfakedata", "--startjson",
                                     getTestFilePath("prop.json"), "2"};
    hidl_handle fd = {};
    int memfd = createMemfd(&fd);
    // Clear existing events.
    mEventQueue.flush();

    EXPECT_FALSE(mHal->dump(fd, options));

    lseek(memfd, 0, SEEK_SET);
    char buf[10240] = {};
    // The dumped info should be empty.
    read(memfd, buf, sizeof(buf));
    EXPECT_STREQ("", buf);

    // wait for some time.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto events = mEventQueue.flush();
    ASSERT_EQ((size_t)8, events.size());
    // First set of events, we test 1st and the last.
    EXPECT_EQ((size_t)1, events[0]->value.int32Values.size());
    EXPECT_EQ(8, events[0]->value.int32Values[0]);
    EXPECT_EQ((size_t)1, events[3]->value.int32Values.size());
    EXPECT_EQ(4, events[3]->value.int32Values[0]);
    // Second set of the same events.
    EXPECT_EQ((size_t)1, events[4]->value.int32Values.size());
    EXPECT_EQ(8, events[4]->value.int32Values[0]);
    EXPECT_EQ((size_t)1, events[7]->value.int32Values.size());
    EXPECT_EQ(4, events[7]->value.int32Values[0]);
}

TEST_F(DefaultVhalImplTest, testDebugGenFakeDataKeyPress) {
    hidl_vec<hidl_string> options = {"--debughal", "--genfakedata", "--keypress", "1", "2"};
    hidl_handle fd = {};
    int memfd = createMemfd(&fd);
    // Clear existing events.
    mEventQueue.flush();

    EXPECT_FALSE(mHal->dump(fd, options));

    lseek(memfd, 0, SEEK_SET);
    char buf[10240] = {};
    // The dumped info should be empty.
    read(memfd, buf, sizeof(buf));
    EXPECT_STREQ("", buf);

    auto events = mEventQueue.flush();
    ASSERT_EQ((size_t)2, events.size());
    EXPECT_EQ(toInt(VehicleProperty::HW_KEY_INPUT), events[0]->prop);
    EXPECT_EQ(toInt(VehicleProperty::HW_KEY_INPUT), events[1]->prop);
    ASSERT_EQ((size_t)3, events[0]->value.int32Values.size());
    ASSERT_EQ((size_t)3, events[1]->value.int32Values.size());
    EXPECT_EQ(toInt(VehicleHwKeyInputAction::ACTION_DOWN), events[0]->value.int32Values[0]);
    EXPECT_EQ(1, events[0]->value.int32Values[1]);
    EXPECT_EQ(2, events[0]->value.int32Values[2]);
    EXPECT_EQ(toInt(VehicleHwKeyInputAction::ACTION_UP), events[1]->value.int32Values[0]);
    EXPECT_EQ(1, events[1]->value.int32Values[1]);
    EXPECT_EQ(2, events[1]->value.int32Values[2]);
}

TEST_F(DefaultVhalImplTest, testHeartBeatEvent) {
    // A heart beat would be sent every 3s, but let's wait for 6s to be sure at least 2 events have
    // been generated (at 0s and 3s).
    std::this_thread::sleep_for(std::chrono::milliseconds(6000));

    auto events = mHeartBeatQueue.flush();
    ASSERT_GE(events.size(), (size_t)2);
    ASSERT_EQ(toInt(VehicleProperty::VHAL_HEARTBEAT), events[0]->prop);
}

}  // namespace