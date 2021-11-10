"""This file holds the code that can be shared between tracers."""

from abc import ABC, abstractmethod
from copy import deepcopy
from typing import Any, Callable, Iterable, List, Optional, Tuple, Type, Union

from ..data_types import Float
from ..data_types.base import BaseDataType
from ..debugging.custom_assert import assert_true
from ..representation.intermediate import (
    IR_MIX_VALUES_FUNC_ARG_NAME,
    Add,
    Constant,
    GenericFunction,
    IndexConstant,
    IntermediateNode,
    Mul,
    Sub,
)
from ..values import BaseValue


class BaseTracer(ABC):
    """Base class for implementing tracers."""

    inputs: List["BaseTracer"]
    traced_computation: IntermediateNode
    output_idx: int
    output: BaseValue
    _mix_values_func: Callable[..., BaseValue]

    def __init__(
        self,
        inputs: Iterable["BaseTracer"],
        traced_computation: IntermediateNode,
        output_idx: int,
    ) -> None:
        self.inputs = list(inputs)
        self.traced_computation = traced_computation
        self.output_idx = output_idx
        self.output = traced_computation.outputs[output_idx]

    @abstractmethod
    def _supports_other_operand(self, other: Any) -> bool:
        """Check if the current class supports tracing with the other operand.

        Args:
            other (Any): the operand to check compatibility with.

        Returns:
            bool: True if the tracer can manage operations with the other operand.
        """
        return isinstance(other, self.__class__)

    @abstractmethod
    def _make_const_input_tracer(self, constant_data: Any) -> "BaseTracer":
        """Create a tracer for a constant input.

        Args:
            constant_data (Any): The constant to store.

        Returns:
            BaseTracer: The BaseTracer for that constant.
        """

    @classmethod
    def _get_mix_values_func(cls):
        return cls._mix_values_func

    def _sanitize(self, inp) -> "BaseTracer":
        if not isinstance(inp, BaseTracer):
            return self._make_const_input_tracer(inp)
        return inp

    def instantiate_output_tracers(
        self,
        inputs: Iterable[Union["BaseTracer", Any]],
        computation_to_trace: Type[IntermediateNode],
    ) -> Tuple["BaseTracer", ...]:
        """Instantiate all output BaseTracer for a given computation.

        Args:
            inputs (Iterable[Union[BaseTracer, Any]]): Previous BaseTracer or data used as inputs
                for a new node.
            computation_to_trace (Type[IntermediateNode]): The IntermediateNode class
                to instantiate for the computation being traced

        Returns:
            Tuple[BaseTracer, ...]: A tuple containing an BaseTracer per output function
        """

        # For inputs which are actually constant, first convert into a tracer
        sanitized_inputs = [self._sanitize(inp) for inp in inputs]

        additional_parameters = (
            {IR_MIX_VALUES_FUNC_ARG_NAME: self._get_mix_values_func()}
            if computation_to_trace.requires_mix_values_func()
            else {}
        )

        traced_computation = computation_to_trace(
            (x.output for x in sanitized_inputs),
            **additional_parameters,
        )

        output_tracers = tuple(
            self.__class__(sanitized_inputs, traced_computation, output_idx)
            for output_idx in range(len(traced_computation.outputs))
        )

        return output_tracers

    def __add__(self, other: Union["BaseTracer", Any]) -> "BaseTracer":
        if not self._supports_other_operand(other):
            return NotImplemented

        result_tracer = self.instantiate_output_tracers(
            [self, other],
            Add,
        )

        assert_true(len(result_tracer) == 1)
        return result_tracer[0]

    # With that is that x + 1 and 1 + x have the same graph. If we want to keep
    # the order, we need to do as in __rsub__, ie mostly a copy of __sub__ +
    # some changes
    __radd__ = __add__

    def __neg__(self) -> "BaseTracer":
        return 0 - self

    def __sub__(self, other: Union["BaseTracer", Any]) -> "BaseTracer":
        if not self._supports_other_operand(other):
            return NotImplemented

        result_tracer = self.instantiate_output_tracers(
            [self, other],
            Sub,
        )

        assert_true(len(result_tracer) == 1)
        return result_tracer[0]

    def __rsub__(self, other: Union["BaseTracer", Any]) -> "BaseTracer":
        if not self._supports_other_operand(other):
            return NotImplemented

        result_tracer = self.instantiate_output_tracers(
            [other, self],
            Sub,
        )

        assert_true(len(result_tracer) == 1)
        return result_tracer[0]

    def __mul__(self, other: Union["BaseTracer", Any]) -> "BaseTracer":
        if not self._supports_other_operand(other):
            return NotImplemented

        result_tracer = self.instantiate_output_tracers(
            [self, other],
            Mul,
        )

        assert_true(len(result_tracer) == 1)
        return result_tracer[0]

    # With that is that x * 3 and 3 * x have the same graph. If we want to keep
    # the order, we need to do as in __rmul__, ie mostly a copy of __mul__ +
    # some changes
    __rmul__ = __mul__

    def __getitem__(self, item):
        traced_computation = IndexConstant(self.output, item)
        return self.__class__([self], traced_computation, 0)

    def _div_common(
        self,
        lhs: Union["BaseTracer", Any],
        rhs: Union["BaseTracer", Any],
        div_op: Callable,
        op_name: str,
        output_dtype: Optional[BaseDataType] = None,
    ) -> "BaseTracer":
        if isinstance(lhs, BaseTracer):
            if not self._supports_other_operand(rhs):
                return NotImplemented
        elif isinstance(rhs, BaseTracer):
            if not self._supports_other_operand(lhs):
                return NotImplemented

        sanitized_inputs = [self._sanitize(inp) for inp in [lhs, rhs]]

        # One of the inputs has to be constant
        if not (
            isinstance(sanitized_inputs[0].traced_computation, Constant)
            or isinstance(sanitized_inputs[1].traced_computation, Constant)
        ):
            raise NotImplementedError(f"Can't manage binary operator {op_name}")

        sanitized_input_values = [san_input.output for san_input in sanitized_inputs]
        output_value = self._get_mix_values_func()(*sanitized_input_values)
        if output_dtype is not None:
            output_value.dtype = deepcopy(output_dtype)

        traced_computation = GenericFunction(
            inputs=sanitized_input_values,
            arbitrary_func=div_op,
            output_value=output_value,
            op_kind="TLU",
            op_name=op_name,
        )

        result_tracer = self.__class__(sanitized_inputs, traced_computation, 0)

        return result_tracer

    def _truediv(
        self, lhs: Union["BaseTracer", Any], rhs: Union["BaseTracer", Any]
    ) -> "BaseTracer":
        return self._div_common(lhs, rhs, lambda x, y: x / y, "truediv", Float(64))

    def __truediv__(self, other: Union["BaseTracer", Any]) -> "BaseTracer":
        return self._truediv(self, other)

    def __rtruediv__(self, other: Union["BaseTracer", Any]) -> "BaseTracer":
        return self._truediv(other, self)

    def _floordiv(
        self, lhs: Union["BaseTracer", Any], rhs: Union["BaseTracer", Any]
    ) -> "BaseTracer":
        return self._div_common(lhs, rhs, lambda x, y: x // y, "floordiv")

    def __floordiv__(self, other: Union["BaseTracer", Any]) -> "BaseTracer":
        return self._floordiv(self, other)

    def __rfloordiv__(self, other: Union["BaseTracer", Any]) -> "BaseTracer":
        return self._floordiv(other, self)
