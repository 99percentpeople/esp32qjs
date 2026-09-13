import unittest


class SourceContractTestCase(unittest.TestCase):
    """Keep source-contract failures useful without dumping complete source files."""

    def assertIn(self, member, container, msg=None):
        if isinstance(member, str) and isinstance(container, str):
            if member not in container:
                self.fail(msg or f"expected source to contain {member!r}")
            return
        super().assertIn(member, container, msg)

    def assertNotIn(self, member, container, msg=None):
        if isinstance(member, str) and isinstance(container, str):
            if member in container:
                self.fail(msg or f"expected source not to contain {member!r}")
            return
        super().assertNotIn(member, container, msg)
