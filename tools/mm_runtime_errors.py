"""Exception taxonomy shared by the Majora runtime tooling modules."""


class RuntimeErrorBase(RuntimeError):
    pass


class RuntimeBusy(RuntimeErrorBase):
    pass


class RuntimeOwnershipError(RuntimeErrorBase):
    pass
