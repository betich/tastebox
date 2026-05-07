class CommandInterceptor:
    """Base class for RS485Bus interceptors. Override only the hooks you need."""

    def before_write(self, addr: int, reg: int, data: list[int]) -> list[int]:
        return data

    def after_write(self, addr: int, reg: int, data: list[int]) -> None:
        pass

    def before_read(self, addr: int, reg: int) -> None:
        pass

    def after_read(self, addr: int, reg: int, value: int) -> int:
        return value
