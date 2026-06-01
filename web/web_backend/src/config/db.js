const mongoose = require('mongoose');

const connectDatabase = async (mongoUri) => {
  return mongoose.connect(mongoUri);
};

module.exports = connectDatabase;
