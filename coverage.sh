# coverage will need to be installed with pip install coverage
# lcov may need to be installed as well. On MacOS - brew install lcov
rm -rf build/
export CFLAGS="-coverage"
python setup.py build_ext --inplace
coverage run --source=./orderbook setup.py test
cd build/temp*
lcov -c --directory . --output-file coverage.info
genhtml coverage.info --output-directory out
open out/index.html