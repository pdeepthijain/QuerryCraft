#include "storage/record.hpp"
#include "storage/schema.hpp"

#include <gtest/gtest.h>

using namespace querycraft::storage;

TEST(RecordTest, ValueTypeVariants) {
    Value null_val;
    EXPECT_TRUE(null_val.is_null());
    EXPECT_EQ(null_val.to_string(), "NULL");

    Value int_val(static_cast<std::int64_t>(42));
    EXPECT_TRUE(int_val.is_int());
    EXPECT_EQ(int_val.as_int(), 42);
    EXPECT_EQ(int_val.to_string(), "42");

    Value float_val(3.14159);
    EXPECT_TRUE(float_val.is_float());
    EXPECT_DOUBLE_EQ(float_val.as_float(), 3.14159);

    Value str_val("QueryCraft");
    EXPECT_TRUE(str_val.is_string());
    EXPECT_EQ(str_val.as_string(), "QueryCraft");
    EXPECT_EQ(str_val.to_string(), "QueryCraft");
}

TEST(RecordTest, IntegerSerialization) {
    Record rec({Value(static_cast<std::int64_t>(100)),
                Value(static_cast<std::int64_t>(-999999999)),
                Value(static_cast<std::int64_t>(0))});

    const auto bytes = rec.serialize();
    EXPECT_FALSE(bytes.empty());

    Record deserialized;
    auto status = Record::deserialize(bytes, deserialized);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(deserialized.value_count(), 3);
    EXPECT_EQ(deserialized.get_value(0).as_int(), 100);
    EXPECT_EQ(deserialized.get_value(1).as_int(), -999999999);
    EXPECT_EQ(deserialized.get_value(2).as_int(), 0);
}

TEST(RecordTest, FloatSerialization) {
    Record rec({Value(9.81), Value(-0.00042), Value(1e9)});

    const auto bytes = rec.serialize();
    Record deserialized;
    auto status = Record::deserialize(bytes, deserialized);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(deserialized.value_count(), 3);
    EXPECT_DOUBLE_EQ(deserialized.get_value(0).as_float(), 9.81);
    EXPECT_DOUBLE_EQ(deserialized.get_value(1).as_float(), -0.00042);
    EXPECT_DOUBLE_EQ(deserialized.get_value(2).as_float(), 1e9);
}

TEST(RecordTest, StringSerialization) {
    Record rec({Value("Short string"),
                Value(""),
                Value("A much longer sentence demonstrating variable length record serialization in QueryCraft.")});

    const auto bytes = rec.serialize();
    Record deserialized;
    auto status = Record::deserialize(bytes, deserialized);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(deserialized.value_count(), 3);
    EXPECT_EQ(deserialized.get_value(0).as_string(), "Short string");
    EXPECT_EQ(deserialized.get_value(1).as_string(), "");
    EXPECT_EQ(deserialized.get_value(2).as_string(),
              "A much longer sentence demonstrating variable length record serialization in QueryCraft.");
}

TEST(RecordTest, NullSerialization) {
    Record rec({Value(nullptr), Value(static_cast<std::int64_t>(123)), Value(nullptr), Value("Active")});

    const auto bytes = rec.serialize();
    Record deserialized;
    auto status = Record::deserialize(bytes, deserialized);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(deserialized.value_count(), 4);
    EXPECT_TRUE(deserialized.get_value(0).is_null());
    EXPECT_EQ(deserialized.get_value(1).as_int(), 123);
    EXPECT_TRUE(deserialized.get_value(2).is_null());
    EXPECT_EQ(deserialized.get_value(3).as_string(), "Active");
}

TEST(RecordTest, SchemaValidationSuccess) {
    Schema schema({
        Column("id", DataType::INT, true, false),
        Column("name", DataType::VARCHAR, false, false, 50),
        Column("gpa", DataType::FLOAT, false, true),
        Column("bio", DataType::TEXT, false, true)
    });

    Record valid_rec({Value(static_cast<std::int64_t>(1)), Value("Alice"), Value(3.95), Value("CS Major")});
    EXPECT_TRUE(valid_rec.validate(schema).ok());

    Record valid_rec_with_null({Value(static_cast<std::int64_t>(2)), Value("Bob"), Value(nullptr), Value(nullptr)});
    EXPECT_TRUE(valid_rec_with_null.validate(schema).ok());
}

TEST(RecordTest, SchemaValidationColumnCountMismatch) {
    Schema schema({
        Column("id", DataType::INT, true, false),
        Column("name", DataType::VARCHAR, false, false, 50)
    });

    Record rec_too_short({Value(static_cast<std::int64_t>(1))});
    EXPECT_FALSE(rec_too_short.validate(schema).ok());

    Record rec_too_long({Value(static_cast<std::int64_t>(1)), Value("Alice"), Value(9.9)});
    EXPECT_FALSE(rec_too_long.validate(schema).ok());
}

TEST(RecordTest, SchemaValidationNullConstraints) {
    Schema schema({
        Column("id", DataType::INT, true, false), // PK cannot be NULL
        Column("name", DataType::VARCHAR, false, false, 50) // NOT NULL
    });

    Record null_pk({Value(nullptr), Value("Alice")});
    EXPECT_FALSE(null_pk.validate(schema).ok());

    Record null_name({Value(static_cast<std::int64_t>(1)), Value(nullptr)});
    EXPECT_FALSE(null_name.validate(schema).ok());
}

TEST(RecordTest, SchemaValidationInvalidTypeInsertion) {
    Schema schema({
        Column("id", DataType::INT, true, false),
        Column("gpa", DataType::FLOAT, false, true),
        Column("name", DataType::TEXT, false, true)
    });

    // String into INT column
    Record bad_int({Value("not an int"), Value(3.5), Value("Valid Name")});
    EXPECT_FALSE(bad_int.validate(schema).ok());

    // String into FLOAT column
    Record bad_float({Value(static_cast<std::int64_t>(1)), Value("three point five"), Value("Valid Name")});
    EXPECT_FALSE(bad_float.validate(schema).ok());

    // INT into TEXT column
    Record bad_text({Value(static_cast<std::int64_t>(1)), Value(3.5), Value(static_cast<std::int64_t>(999))});
    EXPECT_FALSE(bad_text.validate(schema).ok());
}

TEST(RecordTest, SchemaValidationVarcharLengthExceeded) {
    Schema schema({
        Column("code", DataType::VARCHAR, false, false, 5)
    });

    Record valid_len({Value("ABCDE")});
    EXPECT_TRUE(valid_len.validate(schema).ok());

    Record exceeded_len({Value("TOOLONGSTRING")});
    EXPECT_FALSE(exceeded_len.validate(schema).ok());
}

TEST(RecordTest, SchemaSerialization) {
    Schema original({
        Column("student_id", DataType::INT, true, false),
        Column("full_name", DataType::VARCHAR, false, false, 120),
        Column("score", DataType::FLOAT, false, true),
        Column("notes", DataType::TEXT, false, true)
    });

    const auto bytes = original.serialize();
    EXPECT_FALSE(bytes.empty());

    Schema restored;
    auto status = Schema::deserialize(bytes, restored);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(restored.column_count(), 4);
    EXPECT_EQ(restored.get_column(0).name(), "student_id");
    EXPECT_TRUE(restored.get_column(0).is_primary_key());
    EXPECT_EQ(restored.get_column(1).name(), "full_name");
    EXPECT_EQ(restored.get_column(1).length(), 120);
    EXPECT_EQ(restored.get_column(2).type(), DataType::FLOAT);
    EXPECT_EQ(restored.get_column(3).type(), DataType::TEXT);
}
