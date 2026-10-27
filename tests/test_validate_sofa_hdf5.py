import tempfile
import unittest
from pathlib import Path

import h5py
import numpy as np

from scripts.validate_sofa_hdf5 import validate_file


class SofaHdf5ValidationTests(unittest.TestCase):
    def test_valid_sofa_like_hdf5_is_readable(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "valid.sofa"
            with h5py.File(path, "w") as handle:
                handle.attrs["Conventions"] = "SOFA"
                handle.create_dataset("Data.IR", data=np.zeros((2, 2, 64), dtype=np.float32),
                                      chunks=(1, 2, 64), fletcher32=True)
                handle.create_dataset("SourcePosition", data=np.zeros((2, 3), dtype=np.float64))
            self.assertEqual(validate_file(path), [])

    def test_non_hdf5_file_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.sofa"
            path.write_bytes(b"not an HDF5 file")
            errors = validate_file(path)
            self.assertTrue(errors)
            self.assertIn("cannot open/parse HDF5", errors[0])

    def test_non_sofa_convention_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wrong.sofa"
            with h5py.File(path, "w") as handle:
                handle.attrs["Conventions"] = "not SOFA"
                handle.create_dataset("Data.IR", data=np.zeros((1, 2, 16), dtype=np.float32))
            self.assertTrue(any("unexpected convention" in error for error in validate_file(path)))


if __name__ == "__main__":
    unittest.main()
