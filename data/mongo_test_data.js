// Cleanup of databases/collections created during regression run
// As 'test' is a default database, any foreign table created when
// database is not mentioned then corresponding collection gets
// created in test database. So dropping as part of cleanup.
use test
db.mongo_test3.drop();
use mongo_fdw_regress1
db.mongo_test1.drop();
use mongo_fdw_regress2
db.dropDatabase();
use mongo_fdw_regress
db.test_tbl1.drop();
db.test_tbl2.drop();
db.test_tbl3.drop();
db.test_tbl4.drop();
db.test_tbl5.drop();
db.test_tbl7.drop();
db.test_tbl8.drop();
db.test1.drop();
db.test2.drop();
db.test3.drop();
db.test4.drop();
db.mongo_test.drop();
db.test5.drop();
db.test_special_floats.drop();
db.test_semantics.drop();
db.test_semantics_empty.drop();
// Below queries will create and insert values in collections
db.mongo_test.insert({a : NumberInt(0), b : "mongo_test collection"});
db.test_tbl2.insertMany([
   {c1 : NumberInt(10), c2 : "DEVELOPMENT", c3 :"PUNE" },
   {c1: NumberInt(20), c2 : "ADMINISTRATION", c3 :"BANGLORE" },
   {c1: NumberInt(30), c2 : "SALES", c3 :"MUMBAI"  },
   {c1: NumberInt(40), c2 : "HR", c3 :"NAGPUR"  }
]);
db.test_tbl1.insertMany([
   {c1: NumberInt(100), c2 : "EMP1", c3 :"ADMIN", c4 :NumberInt(1300) ,c5 :ISODate("1980-12-17"), c6 :800.300, c7 :NumberInt(0), c8 :NumberInt(20) },
   {c1: NumberInt(200), c2 : "EMP2", c3 :"SALESMAN", c4 :NumberInt(600) ,c5 :ISODate("1981-02-20"), c6 :1600, c7 :NumberInt(300), c8 :NumberInt(30) },
   {c1: NumberInt(300), c2 : "EMP3", c3 :"SALESMAN", c4 :NumberInt(600) ,c5 :ISODate("1981-02-22"), c6 :1250, c7 :NumberInt(500), c8 :NumberInt(30)  },
   {c1: NumberInt(400), c2 : "EMP4", c3 :"MANAGER", c4 :NumberInt(900) ,c5 :ISODate("1981-04-02"), c6 :2975, c7 :NumberInt(0), c8 :NumberInt(20)  },
   {c1: NumberInt(500), c2 : "EMP5", c3 :"SALESMAN", c4 :NumberInt(600) ,c5 :ISODate("1981-09-28"), c6 :1250.23, c7 :NumberInt(1400), c8 :NumberInt(30)  },
   {c1: NumberInt(600), c2 : "EMP6", c3 :"MANAGER", c4 :NumberInt(900) ,c5 :ISODate("1981-05-01"), c6 :2850, c7 :NumberInt(0), c8 :NumberInt(30)  },
   {c1: NumberInt(700), c2 : "EMP7", c3 :"MANAGER", c4 :NumberInt(900) ,c5 :ISODate("1981-06-09"), c6 :2450.34, c7 :NumberInt(0), c8 :NumberInt(10)  },
   {c1: NumberInt(800), c2 : "EMP8", c3 :"FINANCE", c4 :NumberInt(400) ,c5 :ISODate("1987-04-19"), c6 :3000, c7 :NumberInt(0), c8 :NumberInt(20)  },
   {c1: NumberInt(900), c2 : "EMP9", c3 :"HEAD", c4 :null ,c5 :ISODate("1981-11-17"), c6 :5000, c7 :NumberInt(0), c8 :NumberInt(10)  },
   {c1: NumberInt(1000), c2 : "EMP10", c3 :"SALESMAN", c4 :NumberInt(600) ,c5 :ISODate("1980-09-08"), c6 :1500, c7 :NumberInt(0), c8 :NumberInt(30)  },
   {c1: NumberInt(1100), c2 : "EMP11", c3 :"ADMIN", c4 :NumberInt(800) ,c5 :ISODate("1987-05-23"), c6 :1100, c7 :NumberInt(0), c8 :NumberInt(20)  },
   {c1: NumberInt(1200), c2 : "EMP12", c3 :"ADMIN", c4 :NumberInt(600) ,c5 :ISODate("1981-12-03"), c6 :950.00, c7 :NumberInt(0), c8 :NumberInt(30)  },
   {c1: NumberInt(1300), c2 : "EMP13", c3 :"FINANCE", c4 :NumberInt(400) ,c5 :ISODate("1981-12-03"), c6 :3000, c7 :NumberInt(0), c8 :NumberInt(20) },
   {c1: NumberInt(1400), c2 : "EMP14", c3 :"ADMIN", c4 :NumberInt(700) ,c5 :ISODate("1982-01-23"), c6 :1300, c7 :NumberInt(0), c8 :NumberInt(10)  },
]);
db.test_tbl3.insertMany([
   {name: "dvd", marks: [23, 24], pass: false},
   {name: "vdd", marks: [29, 31], pass: true}
]);

db.test1.insertMany([
   {c1: NumberInt(1), c2: NumberInt(1), c3: "A"},
   {c1: NumberInt(2), c2: NumberInt(2), c3: "B"},
   {c1: NumberInt(3), c2: NumberInt(3), c3: "C"},
   {c1: NumberInt(4), c2: NumberInt(4), c3: "D"},
]);

db.test2.insertMany([
   {c1: NumberInt(5), c2: NumberInt(5), c3: "E"},
   {c1: NumberInt(6), c2: NumberInt(6), c3: "F"},
   {c1: NumberInt(7), c2: NumberInt(7), c3: "G"},
   {c1: NumberInt(8), c2: NumberInt(8), c3: "H"},
]);

db.test3.insertMany([
   {c1: NumberInt(1), c2: NumberInt(1), c3: "A"},
   {c1: NumberInt(2), c2: NumberInt(2), c3: "B"},
   {c1: NumberInt(3), c2: NumberInt(3), c3: "C"},
   {c1: NumberInt(4), c2: NumberInt(4), c3: "D"},
]);

db.test4.insertMany([
   {c1: NumberInt(5), c2: NumberInt(5), c3: "E"},
   {c1: NumberInt(6), c2: NumberInt(6), c3: "F"},
   {c1: NumberInt(7), c2: NumberInt(7), c3: "G"},
   {c1: NumberInt(8), c2: NumberInt(8), c3: "H"},
]);

db.test5.insertMany([
   {c1: 12.345678},
   {c1: -1.23}
]);
db.test_tbl4.insertMany([
   {a: NumberInt(25)},
   {a: NumberLong(9999999999)},
   {a: 25},
   {a: 25.09},
   {a: false}
]);
db.test_tbl5.insertMany([
   {a: NumberInt(25)},
   {a: 25},
   {a: 25.09},
   {a: true}
]);
db.test_tbl7.insertMany([
   {a: NumberInt(10), b: "ROW1"},
   {a: NumberInt(20), b: "ROW2"}
]);
db.test_tbl8.insertMany([
   {_id: NumberInt(1), a: NumberInt(2), b: "ROW1"},
   {a: NumberInt(3), b: "ROW2"},
]);
db.mongo_test_large.drop();
db.mongo_test_large.insertMany([
  {_id: NumberInt(0), a01 : NumberInt(1), a02 : NumberInt(2), a03 : NumberInt(3), a04 : NumberInt(4), a05 : NumberInt(5), a06 : NumberInt(6), a07 : NumberInt(7), a08 : NumberInt(8), a09 : NumberInt(9), a10 : NumberInt(10), a11 : NumberInt(11), a12 : NumberInt(12), a13 : NumberInt(13), a14 : NumberInt(14), a15 : NumberInt(15), a16 : NumberInt(16), a17 : NumberInt(17), a18 : NumberInt(18), a19 : NumberInt(19), a20 : NumberInt(20), a21 : NumberInt(21), a22 : NumberInt(22), a23 : NumberInt(23), a24 : NumberInt(24), a25 : NumberInt(25), a26 : NumberInt(26), a27 : NumberInt(27), a28 : NumberInt(28), a29 : NumberInt(29), a30 : NumberInt(30), a31 : NumberInt(31), a32 : NumberInt(32), a33 : NumberInt(33), a34 : NumberInt(134), a35 : NumberInt(35)},
  {_id: NumberInt(1), a01 : NumberInt(1), a02 : NumberInt(2), a03 : NumberInt(3), a04 : NumberInt(4), a05 : NumberInt(5), a06 : NumberInt(6), a07 : NumberInt(7), a08 : NumberInt(8), a09 : NumberInt(9), a10 : NumberInt(10), a11 : NumberInt(11), a12 : NumberInt(12), a13 : NumberInt(13), a14 : NumberInt(14), a15 : NumberInt(15), a16 : NumberInt(16), a17 : NumberInt(17), a18 : NumberInt(18), a19 : NumberInt(19), a20 : NumberInt(20), a21 : NumberInt(21), a22 : NumberInt(22), a23 : NumberInt(23), a24 : NumberInt(24), a25 : NumberInt(25), a26 : NumberInt(26), a27 : NumberInt(27), a28 : NumberInt(28), a29 : NumberInt(29), a30 : NumberInt(30), a31 : NumberInt(31), a32 : NumberInt(2), a33 : NumberInt(3), a34 : NumberInt(4), a35 : NumberInt(5)},
  {_id: NumberInt(2), a01 : NumberInt(1), a02 : NumberInt(2), a03 : NumberInt(3), a04 : NumberInt(4), a05 : NumberInt(5), a06 : NumberInt(6), a07 : NumberInt(7), a08 : NumberInt(8), a09 : NumberInt(9), a10 : NumberInt(10), a11 : NumberInt(11), a12 : NumberInt(12), a13 : NumberInt(13), a14 : NumberInt(14), a15 : NumberInt(15), a16 : NumberInt(16), a17 : NumberInt(17), a18 : NumberInt(18), a19 : NumberInt(19), a20 : NumberInt(20), a21 : NumberInt(21), a22 : NumberInt(22), a23 : NumberInt(23), a24 : NumberInt(24), a25 : NumberInt(25), a26 : NumberInt(26), a27 : NumberInt(27), a28 : NumberInt(28), a29 : NumberInt(29), a30 : NumberInt(30), a31 : NumberInt(31), a32 : NumberInt(132), a33 : NumberInt(133), a34 : NumberInt(134), a35 : NumberInt(135)},
  {_id: NumberInt(3), a01 : NumberInt(1), a02 : NumberInt(2), a03 : NumberInt(3), a04 : NumberInt(4), a05 : NumberInt(5), a06 : NumberInt(6), a07 : NumberInt(7), a08 : NumberInt(8), a09 : NumberInt(9), a10 : NumberInt(10), a11 : NumberInt(11), a12 : NumberInt(12), a13 : NumberInt(13), a14 : NumberInt(14), a15 : NumberInt(15), a16 : NumberInt(16), a17 : NumberInt(17), a18 : NumberInt(18), a19 : NumberInt(19), a20 : NumberInt(20), a21 : NumberInt(21), a22 : NumberInt(22), a23 : NumberInt(23), a24 : NumberInt(24), a25 : NumberInt(25), a26 : NumberInt(26), a27 : NumberInt(27), a28 : NumberInt(28), a29 : NumberInt(29), a30 : NumberInt(30), a31 : NumberInt(31), a32 : NumberInt(32), a33 : NumberInt(3), a34 : NumberInt(34), a35 : NumberInt(35)},
  {_id: NumberInt(4), a01 : NumberInt(1), a02 : NumberInt(2), a03 : NumberInt(3), a04 : NumberInt(4), a05 : NumberInt(5), a06 : NumberInt(6), a07 : NumberInt(7), a08 : NumberInt(8), a09 : NumberInt(9), a10 : NumberInt(10), a11 : NumberInt(11), a12 : NumberInt(12), a13 : NumberInt(13), a14 : NumberInt(14), a15 : NumberInt(15), a16 : NumberInt(16), a17 : NumberInt(17), a18 : NumberInt(18), a19 : NumberInt(19), a20 : NumberInt(20), a21 : NumberInt(21), a22 : NumberInt(22), a23 : NumberInt(23), a24 : NumberInt(24), a25 : NumberInt(25), a26 : NumberInt(26), a27 : NumberInt(27), a28 : NumberInt(28), a29 : NumberInt(29), a30 : NumberInt(30), a31 : NumberInt(31), a32 : NumberInt(32), a33 : NumberInt(33), a34 : NumberInt(34), a35 : NumberInt(35)}
]);

db.test_special_floats.insertMany([
	{_id: NumberInt(1), value: 123.456},
	{_id: NumberInt(2), value: NaN},
	{_id: NumberInt(3), value: Infinity},
	{_id: NumberInt(4), value: -Infinity},
	{_id: NumberInt(5), value: 2147483646.9},
	{_id: NumberInt(6), value: -2147483647.9},
	// INT32 boundary cases
	{_id: NumberInt(7), value: 2147483647.0},     // INT32_MAX
	{_id: NumberInt(8), value: 2147483648.0},     // INT32_MAX + 1
	{_id: NumberInt(9), value: -2147483648.0},    // INT32_MIN
	{_id: NumberInt(10), value: -2147483649.0},   // INT32_MIN - 1
	// Large values for INT64 testing
	{_id: NumberInt(11), value: 1000000000000000.1},
	{_id: NumberInt(12), value: -1000000000000000.1},
	// INT64 boundary cases - the critical edge case
	{_id: NumberInt(113), value: 9223372036854775807.0},  // INT64_MAX
	{_id: NumberInt(13), value: 9223372036854775808.0},   // INT64_MAX + 1
	{_id: NumberInt(14), value: -9223372036854775808.0},  // INT64_MIN
	{_id: NumberInt(114), value: -9223372036854775809.0}, // INT64_MIN - 1
	// Definitively outside the INT64 range
	{_id: NumberInt(15), value: 9223372036854780000.0},
	{_id: NumberInt(16), value: -9223372036854780000.0},
	// Massive scientific notation (Way out of range)
	{_id: NumberInt(17), value: 1.5e30},
	// INT16 (SMALLINT) boundary cases
	{_id: NumberInt(18), value: 32767.0},         // INT16_MAX
	{_id: NumberInt(19), value: 32768.0},         // INT16_MAX + 1
	{_id: NumberInt(20), value: -32768.0},        // INT16_MIN
	{_id: NumberInt(21), value: -32769.0},        // INT16_MIN - 1
	{_id: NumberInt(22), value: 100.5},           // Normal value for SMALLINT
	// Non-double (BSON int32) values feeding the smallint conversion path
	{_id: NumberInt(23), value: NumberInt(30000)},  // Fits SMALLINT
	{_id: NumberInt(24), value: NumberInt(40000)},  // Exceeds SMALLINT, fits INT32
	{_id: NumberInt(25), value: NumberInt(-40000)}, // Exceeds SMALLINT, fits INT32, negative
	// BSON int64 values feeding the smallint conversion path
	{_id: NumberInt(26), value: NumberLong(100)},         // Fits SMALLINT
	{_id: NumberInt(27), value: NumberLong(3000000000)},  // Exceeds INT32 as well
	// Round-to-nearest (not truncate) semantics, matching PostgreSQL's own
	// float-to-integer coercion.
	{_id: NumberInt(28), value: 0.9},              // Rounds to 1, not truncated to 0
	{_id: NumberInt(29), value: -0.9},             // Rounds to -1, not truncated to 0
	{_id: NumberInt(30), value: 2147483647.4},     // Rounds down to INT32_MAX, fits
	{_id: NumberInt(31), value: 2147483647.6},     // Rounds up past INT32_MAX, errors
	{_id: NumberInt(32), value: 2.5},              // Round-half-to-even -> 2
	{_id: NumberInt(33), value: 3.5}               // Round-half-to-even -> 4
]);
// Documents for the pushdown_semantics test: ObjectId and string _ids, and
// fields that are missing, null, or of a type that doesn't match the column.
db.test_semantics.insertMany([
	{_id: ObjectId("62b597048a7fca1c83fc4eea"), g: NumberInt(1), n: NumberInt(3), s: "abc", b: true, x: null},
	{_id: ObjectId("62b597048a7fca1c83fc4eeb"), g: NumberInt(1), n: "10", s: "B", b: false},
	{_id: ObjectId("62b597048a7fca1c83fc4eec"), g: NumberInt(2), s: "a", x: null},
	{_id: "plain-string-id", g: NumberInt(2), n: NumberInt(4), s: "Z", b: null, x: null},
	{_id: "$dollar", g: NumberInt(3), n: NumberInt(-4), s: "$s"}
]);
